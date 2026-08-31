#pragma once
/* ===========================================================================
 * motion - IMU personality: gravity, upside-down, shake  [PHASE 10]
 *
 * WHAT THIS IS NOT: a second IMU driver. It reads through
 * diag_imu_read_screen(), the one accessor that applies the FROZEN raw->screen
 * transform, and then through the SAME display-frame adapter Tilt Maze uses:
 *
 *     tilt_right =  gy
 *     tilt_down  = -gx
 *
 * That adapter is a swap AND a vertical negation, determined on hardware. It
 * is not a tidy 90-degree rotation and it is not to be "simplified" - see
 * HANDOFF 2b. Calibration is applied ON TOP of it as a user offset, never by
 * editing the transform underneath.
 *
 * THE GRAVITY SWITCH gates AMBIENT behaviour only. With it OFF the Visitor
 * stops sliding, stops complaining about being upside down and stops
 * reacting to shakes - but the IMU keeps running, Tilt Maze plays exactly as
 * before, calibration still works, and the diagnostics still stream.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void motion_begin(void);

/* Called every UI tick from main. Cheap and self-throttling. */
void motion_tick(void);

/* True while gravity/upside-down is driving the Visitor, so the wander logic
 * knows to keep its hands off. */
bool motion_active(void);

/* --- calibration --------------------------------------------------------
 * Starts a short capture. Non-blocking: motion_tick() gathers samples and
 * finishes it. Rejects a capture the device was moving through, because a
 * calibration taken mid-wave is worse than none at all. */
void motion_calibrate_start(void);
bool motion_calibrating(void);

/* 0..100. Rises with repeated handling, decays with time. Flavour only: it
 * never reaches an accumulator, a form choice, evolution or visit quality. */
uint8_t motion_annoyance(void);

void motion_report(void);

/* Diagnostic history: arm it, handle the device however you like, then read
 * motion_report(). Exists because a serial capture cannot be synchronised to
 * a human's hands. Off by default; when off it costs nothing. */
void motion_stats_reset(void);
bool motion_stats_on(void);

#ifdef __cplusplus
}
#endif
