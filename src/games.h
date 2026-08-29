#pragma once
/* ===========================================================================
 * games - the four mini-games and their shared shell  [PHASE 7]
 *
 * Flow, identical for every game:
 *   Games page -> intro (records + START) -> play -> result -> Games page
 *
 * Each game owns its own screen, so the pager's gesture handler is not
 * installed while playing. That is a structural fix, not a heuristic: there
 * is simply nothing to disambiguate a game tap against.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "gamerec.h"

#ifdef __cplusplus
extern "C" {
#endif

void games_launch(uint8_t game);
bool games_active(void);
void games_force_exit(void);   /* test hook: leave a game from serial */

/* Sound hooks for Phase 10. They do nothing now; the call sites exist so
 * adding audio later is a body, not a hunt through four games. */
typedef enum {
    SFX_START = 0, SFX_TAP, SFX_HIT, SFX_MISS, SFX_WIN, SFX_LOSE, SFX_TICK
} game_sfx_t;
void games_sfx(game_sfx_t s);

#ifdef __cplusplus
}
#endif
