/* gamerec - game records in their own NVS key. See gamerec.h. */

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "storage.h"
#include "rtc.h"
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

static uint32_t rec_crc(const gamerec_t *r)
{
    return storage_crc32((const uint8_t *)r + CRC_OFF, sizeof(*r) - CRC_OFF);
}

static void defaults(gamerec_t *r)
{
    memset(r, 0, sizeof(*r));
    r->version   = GAMEREC_VERSION;
    r->size      = sizeof(gamerec_t);
    r->last_game = 0xFF;
    r->maze_best_ms = 0;          /* 0 = no time yet */
}

void gamerec_begin(void)
{
    defaults(&s_rec);
    s_open = s_prefs.begin(GAMEREC_NS, false);
    if (!s_open) { Serial.println("GAMEREC: NVS open failed"); return; }

    gamerec_t cand;
    const size_t stored = s_prefs.getBytesLength(GAMEREC_KEY);
    if (stored == 0) { Serial.println("GAMEREC: none yet (first run)"); return; }

    if (stored != sizeof(cand) ||
        s_prefs.getBytes(GAMEREC_KEY, &cand, sizeof(cand)) != sizeof(cand)) {
        Serial.println("GAMEREC: size mismatch -> defaults");
        return;
    }
    if (cand.version != GAMEREC_VERSION) {
        /* No older version exists yet. When one does, migrate here rather
         * than discarding - the chain lives in exactly this spot. */
        Serial.printf("GAMEREC: version %u != %u -> defaults\n",
                      cand.version, GAMEREC_VERSION);
        return;
    }
    if (rec_crc(&cand) != cand.crc32) {
        Serial.println("GAMEREC: bad CRC -> defaults");
        return;
    }
    memcpy(&s_rec, &cand, sizeof(s_rec));
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

    const float mult = gamerec_pending_multiplier(game, now_ts);

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
    uint8_t best = GAME_COUNT; uint16_t n = 0;
    for (uint8_t i = 0; i < GAME_COUNT; i++)
        if (s_rec.plays[i] > n) { n = s_rec.plays[i]; best = i; }
    return best;
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
    if (!delta || !s_rec.last_play_ts) return;
    s_rec.last_play_ts = rtc_shift_ts(s_rec.last_play_ts, delta);
    gamerec_save();
}

void gamerec_reset(void) { defaults(&s_rec); gamerec_save(); }

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
    Serial.println("-----------------------------------------------------------");
}
