#pragma once
/* ===========================================================================
 * gamerec - game records  [PHASE 7]
 *
 * Deliberately a SEPARATE, independently versioned NVS record rather than an
 * extension of save_t. The Phase 5+6 save is proven, sits at 363 of its
 * 384 B budget, and its migration path has been exercised - bolting game
 * history onto it would risk all of that to save one key.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GAME_HILO = 0, GAME_REACT, GAME_MEMORY, GAME_MAZE, GAME_COUNT };

#define GAMEREC_VERSION 2

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t size;
    uint32_t crc32;          /* over everything after this field */

    uint16_t plays[GAME_COUNT];
    uint16_t best[GAME_COUNT];      /* best score; maze uses maze_best_ms */
    uint32_t maze_best_ms;
    uint16_t total_games;

    uint8_t  last_game;      /* 0xFF = none yet                            */
    uint8_t  streak;         /* consecutive plays of last_game             */
    uint32_t last_play_ts;   /* for the 30-minute streak expiry            */

    /* --- v2: THE FAVOURITE GAME, appended -------------------------------
     * v1 is therefore a strict byte prefix of v2 and the migration is "copy
     * the old blob, seed the tail" - the same shape as every save_t schema.
     * The previous loader threw the whole record away on a version bump,
     * which would have cost a child every high score they had set.
     *
     * These are per-VISITOR, which is why they live here rather than in the
     * device-scoped settings: gamerec_reset() at the farewell clears them
     * and the next Visitor rolls its own. */
    uint8_t  favorite;        /* GAME_*; GAME_COUNT = not chosen yet       */
    uint8_t  fav_plays;       /* plays of the favourite since it was chosen */
    uint16_t fav_boredom;     /* hundredths of a play; see config.h        */
    uint16_t fav_bore_target; /* redrawn per favourite, 400..700           */
    uint16_t _pad;
    uint32_t fav_bore_ts;     /* RTC anchor for the time decay             */
} gamerec_t;

/* FROZEN. sizeof(gamerec_t) at v1: 8 header + 8 plays + 8 best + 4 maze +
 * 2 total + 1 last_game + 1 streak + 4 ts. Getting this wrong rejects every
 * v1 record as corrupt - the same trap as SAVE_V4_SIZE. */
#define GAMEREC_V1_SIZE 36

void  gamerec_begin(void);
const gamerec_t *gamerec_get(void);
bool  gamerec_save(void);

/* Record a finished game. Returns the happiness MULTIPLIER that should be
 * applied (1.0 normally, 0.6 once the same game has been repeated). */
float gamerec_record_play(uint8_t game, uint16_t score, uint32_t ms, uint32_t now_ts);

/* What the multiplier WOULD be, without recording - so the result screen can
 * explain the penalty before it is applied. */
float gamerec_pending_multiplier(uint8_t game, uint32_t now_ts);

/* THE CURRENT FAVOURITE. Never GAME_COUNT: if the record has not chosen one
 * yet (a fresh Visitor, or a v1 record migrated forward) this seeds it from
 * personality on the spot. Seeding is lazy rather than done in
 * gamerec_begin() because begin() runs BEFORE persist_load(), so at that
 * point the traits in RAM are a freshly rolled set that the save is about to
 * replace - a favourite chosen there would be weighted by the wrong
 * personality entirely. */
uint8_t     gamerec_favorite(void);

/* Roll a new favourite from personality, excluding `avoid` (pass GAME_COUNT
 * to exclude nothing). Exposed so the console can demonstrate the weighting
 * without playing hundreds of games. */
uint8_t     gamerec_roll_favorite(uint8_t avoid);

/* One-shot: did the favourite just change because of boredom? Returns true
 * ONCE per change and fills in both games. The caller owns the announcement -
 * gamerec deliberately knows nothing about bubbles or dialogue. */
bool        gamerec_take_fav_change(uint8_t *from, uint8_t *to);

/* Is `game` the favourite, and what does that multiply the reward by? */
bool        gamerec_is_favorite(uint8_t game);
float       gamerec_fav_bonus(void);     /* 1.0 + FAV_BONUS_PCT/100 */
const char *gamerec_name(uint8_t game);
/* Move the streak stamp by a CLOCK CORRECTION. See sim_clock_corrected().
 *
 * last_play_ts is an RTC reading, and the repeat-play penalty is the
 * difference between it and now. A +5 day correction would expire a streak
 * that is thirty seconds old; a backward one would underflow the unsigned
 * subtraction and expire it too. Shifting keeps the GAP, which is the only
 * thing this timestamp is ever used for. */
void        gamerec_shift_ts(int32_t delta);

void        gamerec_report(void);
/* Called when the Visitor advances a life stage. Clears the BEST results
 * only - play counts, totals and favourite-game history are kept, because
 * those feed Evolution and the Journal later.
 *
 * Bests reset because they are not comparable across stages: every game gets
 * harder as the Visitor grows, so a Baby's maze time on a wide open maze
 * would stand forever as an Adult record that can never be beaten. */
void        gamerec_on_stage_change(uint8_t new_stage);

void        gamerec_reset(void);

#ifdef __cplusplus
}
#endif
