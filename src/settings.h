#pragma once
/* ===========================================================================
 * settings - device settings and tilt calibration  [PHASE 10]
 *
 * A SEPARATE, VERSIONED NVS RECORD - deliberately not part of `save_t`.
 * Three reasons, in order of how much they would hurt:
 *
 *   1. save_t is nearly full. It is 433 bytes against a 448-byte budget, and
 *      the static_asserts in storage.h fail the build at 448. Volume, a
 *      gravity toggle and a calibration vector would eat most of what is
 *      left, for data that has nothing to do with a Visitor.
 *   2. These settings belong to the DEVICE, not the Visitor. A new Visitor
 *      must not reset the volume the parent chose or the tilt calibration
 *      the child captured, and `X` / a farewell must not wipe them.
 *   3. The pet blob is rewritten every few minutes. Settings change once in
 *      a blue moon and have no business riding that write path - the same
 *      reasoning that put Visit Records in their own key.
 *
 * Versioned and CRC'd like the other side records, so a future field can be
 * appended without discarding what is already stored.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_VERSION 2

/* Opens the record; falls back to documented defaults if absent or corrupt. */
void settings_begin(void);

/* Volume is a VOL_* from audio.h. Kept as a plain uint8_t here so settings
 * does not have to depend on the audio layer. */
uint8_t settings_volume(void);
void    settings_set_volume(uint8_t v);

/* Ambient motion reactions. DEFAULT ON. This gates ambient behaviour ONLY -
 * never the IMU itself, never Tilt Maze, never calibration. */
bool settings_gravity_on(void);
void settings_set_gravity(bool on);

/* Tilt calibration: the neutral reading, in the SAME display frame the maze
 * uses (right = +gy, down = -gx). It sits ABOVE the frozen BSP transform -
 * calibration is a user preference, the transform is hardware truth, and
 * mixing them would make a bad calibration look like a broken board. */
bool settings_cal_valid(void);
void settings_cal(float *right, float *down);
void settings_set_cal(float right, float down);
void settings_clear_cal(void);

/* --- THE CLOCK CONFIRMATION [v1.0.0 pre-release] ------------------------
 * "A human has told this device what the date is, and the write was read
 * back and verified."
 *
 * rtc_trusted() cannot answer that question. It means "the oscillator has
 * not stopped and the reading is inside a plausible window" - which a stale
 * development date, or a date left behind by a diagnostic, satisfies
 * perfectly. Hatching a Visitor against one of those is exactly the bug this
 * flag exists to stop: the age baseline is taken from a wrong clock, and
 * correcting the date afterwards then reads as days of real elapsed life.
 *
 * DEVICE-SCOPED, not part of save_t, for the same three reasons the volume
 * is: the clock belongs to the device, a new Visitor must not make a parent
 * re-enter it, and a farewell or an `X` must not wipe it.
 *
 * Cleared whenever the RTC goes untrusted (a dead backup cell, a stopped
 * oscillator) - at that point the confirmation is about a clock that no
 * longer exists. */
bool     settings_clock_confirmed(void);
uint32_t settings_clock_set_ts(void);     /* what it read when confirmed; 0 if never */
void     settings_set_clock_confirmed(uint32_t confirmed_now_ts);  /* 0 = clear */

void settings_report(void);

#ifdef __cplusplus
}
#endif
