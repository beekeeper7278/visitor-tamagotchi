#pragma once
/* Phase 1 on-screen test card. Deliberately not the game UI - this exists
 * to make four things visually verifiable in one glance:
 *   - the panel is alive and correctly addressed (corner markers)
 *   - LV_COLOR_16_SWAP=0 is right (the RGB swatches must read R/G/B)
 *   - the 16px column offset is right (left/right markers are symmetric)
 *   - touch maps to the same coordinate space as drawing (tap crosshair) */
#ifdef __cplusplus
extern "C" {
#endif
void ui_diag_create(void);
void ui_diag_set_status(const char *txt);
#ifdef __cplusplus
}
#endif
