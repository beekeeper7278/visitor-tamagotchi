#pragma once
/* Phase 1 hardware diagnostics.
 *
 * Every check that can validate its own assumptions does so. Where a
 * register map is taken from a part datasheet rather than from the verified
 * board facts, the check confirms it via a WHO_AM_I / plausibility test and
 * reports "assumption wrong" rather than printing garbage. */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void diag_banner(void);
void diag_i2c_report(void);          /* measurement 1 */
void diag_flush_report(void);        /* measurement 2 */
void diag_lvgl_heap_report(void);    /* measurement 3 */
void diag_imu_begin(void);           /* measurement 4 - setup */
void diag_imu_tick(void);            /* measurement 4 - 10Hz print */
void diag_boot_tick(void);           /* measurement 5 */
void diag_brightness_sweep(void);    /* measurement 6 */
void diag_rtc_report(void);          /* measurement 7 */
void diag_storage_report(void);      /* storage foundation self-test */
void diag_summary(void);

/* Interactive serial console so the whole Phase 1 test can be driven
 * without reflashing between checks. */
void diag_rtc_clear_os(void);   /* measurement 7b */
void diag_imu_capture_start(void); /* measurement 4 - guided axis capture */
void diag_help(void);

/* Phase 9.5: identity, the persisted growth path, dreams, learned behaviour. */
void diag_identity_report(void);
void diag_serial_tick(void);
bool diag_imu_stream_enabled(void);

/* Gravity in SCREEN axes (g), using the frozen verified mapping.
 * +X right, +Y down, +Z into the screen. Used by the tilt maze. */
bool diag_imu_read_screen(float *x, float *y, float *z);

#ifdef __cplusplus
}
#endif
