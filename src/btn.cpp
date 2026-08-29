/* btn - BOOT short-press detection. See btn.h. */

#include <Arduino.h>
#include "board_pins.h"
#include "config.h"
#include "btn.h"

static btn_cb_t  s_cb;
static int       s_stable = 1;      /* debounced level; 1 = released       */
static int       s_last   = 1;
static uint32_t  s_edge_ms;
static uint32_t  s_press_ms;
static bool      s_down;

void btn_init(btn_cb_t on_short_press)
{
    s_cb = on_short_press;
    /* INPUT_PULLUP only. See the strapping rules in board_pins.h - these are
     * permanent, not provisional. */
    pinMode(BSP_BOOT_BTN, INPUT_PULLUP);
    s_stable = s_last = digitalRead(BSP_BOOT_BTN);
    s_edge_ms = millis();
}

void btn_tick(void)
{
    const int raw = digitalRead(BSP_BOOT_BTN);
    const uint32_t now = millis();

    if (raw != s_last) { s_last = raw; s_edge_ms = now; return; }
    if (now - s_edge_ms < BOOT_DEBOUNCE_MS) return;
    if (raw == s_stable) return;

    s_stable = raw;

    const bool pressed = (raw == 0);      /* active low */
    if (pressed) {
        s_down = true;
        s_press_ms = now;
        return;
    }

    if (s_down) {
        s_down = false;
        const uint32_t held = now - s_press_ms;
        if (held <= BOOT_SHORT_PRESS_MAX_MS) {
            Serial.printf("BOOT short press (%lu ms) -> menu toggle\n",
                          (unsigned long)held);
            if (s_cb) s_cb();
        } else {
            /* Long holds are ignored on purpose: that is the bootloader
             * gesture, and it must never also do something in the app. */
            Serial.printf("BOOT held %lu ms - ignored (> %d ms)\n",
                          (unsigned long)held, BOOT_SHORT_PRESS_MAX_MS);
        }
    }
}
