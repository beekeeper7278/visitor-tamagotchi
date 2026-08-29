/* VISITOR - Phase 1: board boot, display, touch, LVGL, storage foundation.
 *
 * loop() stays exactly as specified in the brief and must not grow.
 * All logic runs from LVGL timers. */

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "bsp.h"
#include "storage.h"
#include "diag.h"
#include "ui_diag.h"

static void imu_timer_cb(lv_timer_t *t)  { (void)t; diag_imu_tick();  }
static void boot_timer_cb(lv_timer_t *t) { (void)t; diag_boot_tick(); }
static void cons_timer_cb(lv_timer_t *t) { (void)t; diag_serial_tick(); }

static void heartbeat_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t n = 0;
    lv_mem_monitor_t m; lv_mem_monitor(&m);
    Serial.printf("[hb %lu] lvgl_used=%lu max=%lu frag=%u%% int_free=%lu\n",
                  (unsigned long)++n,
                  (unsigned long)(m.total_size - m.free_size),
                  (unsigned long)m.max_used, m.frag_pct,
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);          /* verified requirement, brief section 2 */
    delay(300);                        /* let USB CDC enumerate before we talk */

    /* GPIO0 observed only - INPUT_PULLUP, never driven. Keeps the strapping
     * pin high at reset so BOOT+RESET bootloader entry keeps working. */
    pinMode(BSP_BOOT_CANDIDATE, INPUT_PULLUP);

    diag_banner();

    bsp_init();
    storage_init();

    diag_i2c_report();          /* 1 */
    diag_storage_report();      /* storage foundation */
    diag_imu_begin();           /* 4 */
    diag_rtc_report();          /* 7 */

    ui_diag_create();
    lv_refr_now(NULL);

    diag_flush_report();        /* 2 - needs a rendered screen */
    diag_lvgl_heap_report();    /* 3 - after the UI exists */
    diag_brightness_sweep();    /* 6 */

    char st[160];
    const bsp_status_t *s = bsp_status();
    snprintf(st, sizeof(st),
             "I2C %u devs   touch %s\nLVGL ok   display %s",
             s->i2c_count,
             s->touch_ok ? "ok" : "FAIL",
             s->display_ok ? "ok" : "BLOCKED");
    ui_diag_set_status(st);

    diag_summary();
    diag_help();

    lv_timer_create(imu_timer_cb,  1000 / DIAG_IMU_PRINT_HZ, NULL);
    lv_timer_create(boot_timer_cb, 20,    NULL);
    lv_timer_create(cons_timer_cb, 30,    NULL);
    lv_timer_create(heartbeat_cb,  10000, NULL);
}

void loop()
{
    lv_timer_handler();
    delay(5);
}
