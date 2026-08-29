#pragma once
/* ===========================================================================
 * setclock - touch date AND time setter  [MILESTONE 5]
 *
 * The date is NOT optional. There is no network time source on this board,
 * and the real calendar date is required for the hatch date, age, elapsed
 * simulation, sleep/day boundaries, Visit Records and farewell dates. An
 * hour/minute-only setter would leave every one of those wrong.
 *
 * 12-hour display with AM/PM, because the audience is a child and a parent,
 * not a 24-hour clock reader.
 * ======================================================================== */

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void setclock_open(void);      /* full-screen modal over the current screen */
bool setclock_is_open(void);

#ifdef __cplusplus
}
#endif
