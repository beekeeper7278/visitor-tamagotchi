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

#define GAMEREC_VERSION 1

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
} gamerec_t;

void  gamerec_begin(void);
const gamerec_t *gamerec_get(void);
bool  gamerec_save(void);

/* Record a finished game. Returns the happiness MULTIPLIER that should be
 * applied (1.0 normally, 0.6 once the same game has been repeated). */
float gamerec_record_play(uint8_t game, uint16_t score, uint32_t ms, uint32_t now_ts);

/* What the multiplier WOULD be, without recording - so the result screen can
 * explain the penalty before it is applied. */
float gamerec_pending_multiplier(uint8_t game, uint32_t now_ts);

uint8_t     gamerec_favorite(void);      /* most-played; GAME_COUNT if none */
const char *gamerec_name(uint8_t game);
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
