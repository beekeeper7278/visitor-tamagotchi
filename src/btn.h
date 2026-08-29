#pragma once
/* ===========================================================================
 * btn - BOOT button: debounce and SHORT-press detection
 *
 * GPIO0 is also the ROM bootloader strapping pin, so a short press is
 * explicitly bounded: anything longer than BOOT_SHORT_PRESS_MAX_MS is
 * ignored, which is what stops a deliberate bootloader hold registering as a
 * menu toggle. The pin is only ever read - configured INPUT_PULLUP, never
 * driven, never pulled low in software.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*btn_cb_t)(void);

void btn_init(btn_cb_t on_short_press);
void btn_tick(void);        /* poll; called from an LVGL timer */

#ifdef __cplusplus
}
#endif
