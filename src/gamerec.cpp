/* gamerec - game records in their own NVS key. See gamerec.h. */

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "storage.h"
#include "rtc.h"
#include "pet.h"
#include "evolve.h"
#include "gamerec.h"

#define GAMEREC_NS   "visitorg"
#define GAMEREC_KEY  "grec"

/* Streak expiry. The repetition penalty is about "you just played this four
 * times in a row", not "you played it this month". */
#define STREAK_EXPIRY_SEC   (30 * 60)
#define STREAK_PENALTY_AT   3
#define STREAK_MULTIPLIER   0.6f

static Preferences s_prefs;
static gamerec_t   s_rec;
static bool        s_open;

static const size_t CRC_OFF = offsetof(gamerec_t, crc32) + sizeof(uint32_t);

/* CRC over everything after the crc32 field, for a record of `len` bytes.
 * The length is a parameter so a v1 blob can be checked against v1's own
 * extent while sitting in a v2-sized struct. */
static uint32_t rec_crc_len(const gamerec_t *r, size_t len)
{
    return storage_crc32((const uint8_t *)r + CRC_OFF, len - CRC_OFF);
}
static uint32_t rec_crc(const gamerec_t *r) { return rec_crc_len(r, sizeof(*r)); }

static_assert(sizeof(gamerec_t) == GAMEREC_V1_SIZE + 12,
              "gamerec v2 must APPEND exactly 12 bytes to v1");
static_assert(offsetof(gamerec_t, favorite) == GAMEREC_V1_SIZE,
              "the v2 tail must start exactly where v1 ended");

static void defaults(gamerec_t *r)
{
    memset(r, 0, sizeof(*r));
    r->version   = GAMEREC_VERSION;
    r->size      = sizeof(gamerec_t);
    r->last_game = 0xFF;
    r->maze_best_ms = 0;          /* 0 = no time yet */
    /* NOT chosen here. defaults() runs inside gamerec_begin(), before
     * persist_load() has restored the real personality - see gamerec.h. The
     * sentinel means "seed me on first use". */
    r->favorite  = GAME_COUNT;
}

/* --- THE FAVOURITE ------------------------------------------------------ */

/* One row per trait, in PERS_* order, each in GAME_* order. Assembled here
 * rather than in config.h because only this file knows the two enums; every
 * NUMBER still lives in config.h so the tuning stays in one place. */
static const int16_t FAV_W[PERS_COUNT][GAME_COUNT] = {
    FAV_W_PLAYFUL, FAV_W_SLEEPY,      FAV_W_DRAMATIC,
    FAV_W_TIDY,    FAV_W_MISCHIEVOUS, FAV_W_FOODIE,
    FAV_W_COMPETITIVE, FAV_W_CURIOUS, FAV_W_SHY,
};
/* If a trait is ever added, this fails the build rather than silently
 * reading past the end of the table. */
static_assert(sizeof(FAV_W) / sizeof(FAV_W[0]) == PERS_COUNT,
              "FAV_W needs one row per personality trait");

static uint16_t fav_draw_target(void)
{
    return (uint16_t)random(FAV_BORE_TARGET_MIN, FAV_BORE_TARGET_MAX + 1);
}

uint8_t gamerec_roll_favorite(uint8_t avoid)
{
    const pet_state_t *p = pet_get();
    int16_t w[GAME_COUNT];
    int32_t total = 0;

    for (uint8_t g = 0; g < GAME_COUNT; g++) {
        int16_t v = FAV_W_BASE;
        if (p->trait_a < PERS_COUNT) v = (int16_t)(v + FAV_W[p->trait_a][g]);
        if (p->trait_b < PERS_COUNT) v = (int16_t)(v + FAV_W[p->trait_b][g]);
        if (v < FAV_W_MIN) v = FAV_W_MIN;   /* nothing is ever impossible */
        if (g == avoid)   v = 0;            /* ...except the one we are leaving */
        w[g] = v;
        total += v;
    }
    if (total <= 0) {                       /* only reachable if avoid ate it all */
        uint8_t g = (uint8_t)random(0, GAME_COUNT);
        if (g == avoid) g = (uint8_t)((g + 1) % GAME_COUNT);
        return g;
    }

    int32_t roll = random(0, total);
    for (uint8_t g = 0; g < GAME_COUNT; g++) {
        roll -= w[g];
        if (roll < 0) return g;
    }
    return (uint8_t)((avoid + 1) % GAME_COUNT);   /* unreachable; never NONE */
}

/* Seed on first use. Idempotent, and the ONLY place a favourite comes into
 * existence outside a boredom switch. */
static void fav_seed_if_needed(void)
{
    if (s_rec.favorite < GAME_COUNT) return;
    s_rec.favorite        = gamerec_roll_favorite(GAME_COUNT);
    s_rec.fav_plays       = 0;
    s_rec.fav_boredom     = 0;
    s_rec.fav_bore_target = fav_draw_target();
    s_rec.fav_bore_ts     = rtc_trusted() ? rtc_now() : 0;
    const pet_state_t *p = pet_get();
    Serial.printf("FAVOURITE: %s chosen for a %s/%s Visitor "
                  "(bored after %u.%02u plays)\n",
                  gamerec_name(s_rec.favorite),
                  evolve_trait_name(p->trait_a), evolve_trait_name(p->trait_b),
                  s_rec.fav_bore_target / 100, s_rec.fav_bore_target % 100);
    gamerec_save();
}

/* Time decay, anchored to the RTC so it cannot be reset by rebooting. */
static void fav_decay_to(uint32_t now_ts)
{
    if (!now_ts || !s_rec.fav_bore_ts || now_ts <= s_rec.fav_bore_ts) {
        if (now_ts) s_rec.fav_bore_ts = now_ts;
        return;
    }
    uint32_t secs = now_ts - s_rec.fav_bore_ts;
    /* CLAMPED before the multiply. secs * FAV_BORE_DECAY_PER_HOUR overflows
     * uint32 somewhere past five years of elapsed time, and a wrapped
     * product would ADD boredom instead of removing it. 100 hours is already
     * far more decay than the maximum target, so clamping costs nothing and
     * removes the failure mode entirely. */
    if (secs > 360000UL) secs = 360000UL;
    const uint32_t off  = (secs * (uint32_t)FAV_BORE_DECAY_PER_HOUR) / 3600UL;
    if (off) {
        s_rec.fav_boredom = (uint16_t)((s_rec.fav_boredom > off)
                                       ? s_rec.fav_boredom - off : 0);
        s_rec.fav_bore_ts = now_ts;
    }
}

static uint8_t s_fav_from = GAME_COUNT, s_fav_to = GAME_COUNT;
static bool    s_fav_changed;

bool gamerec_take_fav_change(uint8_t *from, uint8_t *to)
{
    if (!s_fav_changed) return false;
    s_fav_changed = false;
    if (from) *from = s_fav_from;
    if (to)   *to   = s_fav_to;
    return true;
}

bool  gamerec_is_favorite(uint8_t game) { return game == gamerec_favorite(); }
float gamerec_fav_bonus(void)           { return 1.0f + (float)FAV_BONUS_PCT / 100.0f; }

void gamerec_begin(void)
{
    defaults(&s_rec);
    s_open = s_prefs.begin(GAMEREC_NS, false);
    if (!s_open) { Serial.println("GAMEREC: NVS open failed"); return; }

    const size_t stored = s_prefs.getBytesLength(GAMEREC_KEY);
    if (stored == 0) { Serial.println("GAMEREC: none yet (first run)"); return; }

    if (stored != sizeof(gamerec_t) && stored != GAMEREC_V1_SIZE) {
        Serial.printf("GAMEREC: unusable size %u -> defaults\n", (unsigned)stored);
        return;
    }

    /* Read into a v2-sized candidate that STARTS as defaults, so a v1 blob
     * fills the prefix and leaves the appended tail at its sentinel - which
     * is exactly the migration. This is the spot the v1 comment reserved for
     * the chain; discarding here would have cost a child every high score. */
    gamerec_t cand;
    defaults(&cand);
    if (s_prefs.getBytes(GAMEREC_KEY, &cand, stored) != stored) {
        Serial.println("GAMEREC: read failed -> defaults");
        return;
    }

    const bool plausible = (cand.size == stored) &&
                           ((cand.version == 1 && stored == GAMEREC_V1_SIZE) ||
                            (cand.version == GAMEREC_VERSION &&
                             stored == sizeof(gamerec_t)));
    if (!plausible) {
        Serial.printf("GAMEREC: rejected (v%u size %u) -> defaults\n",
                      cand.version, cand.size);
        return;
    }
    if (rec_crc_len(&cand, stored) != cand.crc32) {
        Serial.println("GAMEREC: bad CRC -> defaults");
        return;
    }

    memcpy(&s_rec, &cand, sizeof(s_rec));
    if (cand.version != GAMEREC_VERSION) {
        /* favorite stays GAME_COUNT: the migrated Visitor picks one on first
         * use, weighted by the personality the save is about to restore. */
        s_rec.version = GAMEREC_VERSION;
        s_rec.size    = sizeof(gamerec_t);
        Serial.printf("GAMEREC: migrated v%u -> v%u, scores and play history "
                      "kept; favourite will be chosen on first use\n",
                      cand.version, GAMEREC_VERSION);
        gamerec_save();
    }
    Serial.printf("GAMEREC: loaded, %u games played\n", s_rec.total_games);
}

const gamerec_t *gamerec_get(void) { return &s_rec; }

bool gamerec_save(void)
{
    if (!s_open) return false;
    s_rec.version = GAMEREC_VERSION;
    s_rec.size    = sizeof(s_rec);
    s_rec.crc32   = rec_crc(&s_rec);
    return s_prefs.putBytes(GAMEREC_KEY, &s_rec, sizeof(s_rec)) == sizeof(s_rec);
}

float gamerec_pending_multiplier(uint8_t game, uint32_t now_ts)
{
    if (s_rec.last_game != game) return 1.0f;
    if (now_ts && s_rec.last_play_ts &&
        (now_ts - s_rec.last_play_ts) > (uint32_t)STREAK_EXPIRY_SEC) return 1.0f;
    return (s_rec.streak >= STREAK_PENALTY_AT) ? STREAK_MULTIPLIER : 1.0f;
}

float gamerec_record_play(uint8_t game, uint16_t score, uint32_t ms, uint32_t now_ts)
{
    if (game >= GAME_COUNT) return 1.0f;

    fav_seed_if_needed();
    float mult = gamerec_pending_multiplier(game, now_ts);

    /* --- THE FAVOURITE: reward, then boredom ---------------------------
     * The bonus multiplies the same term the repeat penalty does, so the two
     * compose and NEITHER can reach the score. Scores and bests are written
     * further down from the raw `score`, untouched by any multiplier. */
    const bool was_fav = (game == s_rec.favorite);
    if (was_fav) mult *= gamerec_fav_bonus();

    fav_decay_to(now_ts);
    if (was_fav) {
        if (s_rec.fav_plays < 255) s_rec.fav_plays++;
        const uint32_t b = (uint32_t)s_rec.fav_boredom + FAV_BORE_PER_PLAY;
        s_rec.fav_boredom = (uint16_t)(b > 65535U ? 65535U : b);
    } else {
        /* Playing something else costs MORE boredom than a favourite play
         * adds, so alternating can never accumulate - see config.h. */
        s_rec.fav_boredom = (uint16_t)((s_rec.fav_boredom > FAV_BORE_PER_OTHER)
                                       ? s_rec.fav_boredom - FAV_BORE_PER_OTHER : 0);
    }

    if (s_rec.fav_boredom >= s_rec.fav_bore_target) {
        const uint8_t from = s_rec.favorite;
        /* Captured BEFORE the reset below: the log used to read this after
         * zeroing it and always claimed "after 0 plays". */
        const uint8_t played = s_rec.fav_plays;
        /* EXCLUDES the current favourite, so boredom always actually moves
         * somewhere - re-picking the game it just got bored of would make
         * the whole mechanic invisible. */
        s_rec.favorite        = gamerec_roll_favorite(from);
        s_rec.fav_plays       = 0;
        s_rec.fav_boredom     = 0;
        s_rec.fav_bore_target = fav_draw_target();
        s_rec.fav_bore_ts     = now_ts;
        s_fav_from = from; s_fav_to = s_rec.favorite; s_fav_changed = true;
        Serial.printf("FAVOURITE: bored of %s after %u plays -> %s "
                      "(next switch at %u.%02u plays)\n",
                      gamerec_name(from), (unsigned)played,
                      gamerec_name(s_rec.favorite),
                      s_rec.fav_bore_target / 100, s_rec.fav_bore_target % 100);
    }

    /* streak bookkeeping: a different game resets it, and so does 30 minutes */
    const bool expired = (now_ts && s_rec.last_play_ts &&
                          (now_ts - s_rec.last_play_ts) > (uint32_t)STREAK_EXPIRY_SEC);
    if (s_rec.last_game != game || expired) s_rec.streak = 1;
    else if (s_rec.streak < 255)            s_rec.streak++;

    s_rec.last_game    = game;
    s_rec.last_play_ts = now_ts;

    if (s_rec.plays[game] < 65535) s_rec.plays[game]++;
    if (s_rec.total_games < 65535) s_rec.total_games++;
    if (score > s_rec.best[game]) s_rec.best[game] = score;

    /* Maze is scored by TIME, so lower is better and 0 means "no time yet". */
    if (game == GAME_MAZE && ms > 0 &&
        (s_rec.maze_best_ms == 0 || ms < s_rec.maze_best_ms))
        s_rec.maze_best_ms = ms;

    gamerec_save();
    return mult;
}

uint8_t gamerec_favorite(void)
{
    /* NEVER GAME_COUNT. It used to return the most-played game, which was a
     * readout of the player's habit rather than a trait of the Visitor, and
     * was "-" until the first game was finished. */
    fav_seed_if_needed();
    return s_rec.favorite;
}

const char *gamerec_name(uint8_t g)
{
    switch (g) {
        case GAME_HILO:   return "Higher/Lower";
        case GAME_REACT:  return "Reaction";
        case GAME_MEMORY: return "Memory";
        case GAME_MAZE:   return "Tilt Maze";
        default:          return "-";
    }
}

void gamerec_on_stage_change(uint8_t new_stage)
{
    for (uint8_t i = 0; i < GAME_COUNT; i++) s_rec.best[i] = 0;
    s_rec.maze_best_ms = 0;
    /* plays[], total_games, last_game and the streak all survive: they are
     * history, not scores. */
    gamerec_save();
    Serial.printf("GAMEREC: best results reset for the new stage (%u) - "
                  "play history kept\n", new_stage);
}

void gamerec_shift_ts(int32_t delta)
{
    if (!delta) return;
    /* BOTH stamps. fav_bore_ts anchors the boredom time-decay, so leaving it
     * behind on a +5 day correction would decay five days of boredom that
     * never happened - the same class of bug sim_clock_corrected() exists to
     * prevent, in a record it does not own. */
    if (!s_rec.last_play_ts && !s_rec.fav_bore_ts) return;
    s_rec.last_play_ts = rtc_shift_ts(s_rec.last_play_ts, delta);
    s_rec.fav_bore_ts  = rtc_shift_ts(s_rec.fav_bore_ts,  delta);
    gamerec_save();
}

void gamerec_reset(void)
{
    defaults(&s_rec);
    /* The farewell calls pet_init() BEFORE this, so the new Visitor's
     * personality already exists and its favourite can be rolled now rather
     * than waiting for the first game. */
    fav_seed_if_needed();
    gamerec_save();
}

void gamerec_report(void)
{
    Serial.println();
    Serial.println("=== GAME RECORDS ==========================================");
    for (uint8_t i = 0; i < GAME_COUNT; i++)
        Serial.printf("  %-13s plays %-4u best %u\n",
                      gamerec_name(i), s_rec.plays[i], s_rec.best[i]);
    if (s_rec.maze_best_ms)
        Serial.printf("  maze best time: %lu.%02lus\n",
                      (unsigned long)(s_rec.maze_best_ms / 1000),
                      (unsigned long)((s_rec.maze_best_ms % 1000) / 10));
    Serial.printf("  total %u   favourite %s\n", s_rec.total_games,
                  gamerec_name(gamerec_favorite()));
    Serial.printf("  streak: %s x%u  (penalty at %d, expires after %d min)\n",
                  gamerec_name(s_rec.last_game), s_rec.streak,
                  STREAK_PENALTY_AT, STREAK_EXPIRY_SEC / 60);

    /* THE FAVOURITE, and why it is where it is. Printed as plays rather than
     * raw boredom units so the "4 to 7 plays" rule can be read off directly. */
    {
        const pet_state_t *p = pet_get();
        Serial.printf("  FAVOURITE %s   bonus x%.2f on happiness only\n",
                      gamerec_name(gamerec_favorite()), (double)gamerec_fav_bonus());
        Serial.printf("    boredom %u.%02u of %u.%02u plays   (%u plays of it so far)\n",
                      s_rec.fav_boredom / 100, s_rec.fav_boredom % 100,
                      s_rec.fav_bore_target / 100, s_rec.fav_bore_target % 100,
                      (unsigned)s_rec.fav_plays);
        Serial.printf("    +%d per play of it, -%d per other game, -%d per hour\n",
                      FAV_BORE_PER_PLAY, FAV_BORE_PER_OTHER, FAV_BORE_DECAY_PER_HOUR);
        /* The live odds, from the SAME table the roll uses, so a wrong weight
         * shows up as a wrong number rather than as a feeling. */
        Serial.printf("    odds for a %s/%s Visitor:",
                      evolve_trait_name(p->trait_a), evolve_trait_name(p->trait_b));
        int32_t tot = 0; int16_t w[GAME_COUNT];
        for (uint8_t g = 0; g < GAME_COUNT; g++) {
            int16_t v = FAV_W_BASE;
            if (p->trait_a < PERS_COUNT) v = (int16_t)(v + FAV_W[p->trait_a][g]);
            if (p->trait_b < PERS_COUNT) v = (int16_t)(v + FAV_W[p->trait_b][g]);
            if (v < FAV_W_MIN) v = FAV_W_MIN;
            w[g] = v; tot += v;
        }
        for (uint8_t g = 0; g < GAME_COUNT; g++)
            Serial.printf("  %s %d%%", gamerec_name(g),
                          tot ? (int)((w[g] * 100 + tot / 2) / tot) : 0);
        Serial.println();
    }
    Serial.println("-----------------------------------------------------------");
}
