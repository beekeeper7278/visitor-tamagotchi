/* VISITOR - Phase 2: the pet on screen - sprite, animation, speech bubbles.
 *
 * Phase 1 (board boot, display, touch, LVGL, storage) is frozen and still
 * reachable: press D for the Phase 1 test card, P to come back.
 *
 * loop() stays exactly as specified in the brief and must not grow.
 * All logic runs from LVGL timers. */

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "forms.h"
#include "config.h"
#include "bsp.h"
#include "storage.h"
#include "diag.h"
#include "ui_diag.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "scr_main.h"
#include "strings.h"
#include "pet.h"
#include "care.h"
#include "pages.h"
#include "menu.h"
#include "btn.h"
#include "rtc.h"
#include "sim.h"
#include "persist.h"
#include "gamerec.h"
#include "games.h"
#include "discipline.h"
#include "evolve.h"
#include "journal.h"
#include "visitrec.h"
#include "farewell.h"
#include "dialogue.h"

static void imu_timer_cb(lv_timer_t *t)  { (void)t; diag_imu_tick();  }
static void boot_timer_cb(lv_timer_t *t) { (void)t; diag_boot_tick(); btn_tick(); }
static void cons_timer_cb(lv_timer_t *t) { (void)t; diag_serial_tick(); }

/* t_anim - 10 fps. The pet's whole animation state machine and the bubble
 * expiry both run from here. Nothing is created or deleted per frame, so
 * only dirty rectangles are redrawn. */
/* t_sim - 1 s. Time-driven, not tick-driven: care_tick() computes its own dt
 * so a starved timer loses no simulated time. */
static void sim_timer_cb(lv_timer_t *t)
{
    (void)t;
    care_tick();
    discipline_tick();
    if (farewell_due()) farewell_begin();
    persist_tick();
    scr_main_hud_refresh();
    scr_main_egg_refresh();

    /* Feed real pet state into the renderer's live modifiers, so weight and
     * cleanliness actually change how the pet looks rather than only how the
     * Stats bars read. */
    pet_live_t lv;
    lv.weight_norm = pet_weight_norm();
    lv.cleanliness = (uint8_t)pet_get()->cleanliness;
    ui_pet_set_live(&lv);
}

/* t_save - the periodic dirty-flag save. storage_save() still enforces the
 * minimum interval and the shadow compare, so a quiet pet writes nothing. */
static void save_timer_cb(lv_timer_t *t) { (void)t; persist_save(false); }

static void anim_timer_cb(lv_timer_t *t)
{
    (void)t;
    ui_pet_tick();
    ui_bubble_tick();
    care_anim_tick();
    scr_main_sleep_fx();
}

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

    /* Both screens are built once. The pet screen is the boot default; the
     * Phase 1 test card stays resident so the hardware regression surface is
     * one keypress away and costs nothing to keep. */
    pet_init();
    btn_init(menu_toggle);
    rtc_begin();
    gamerec_begin();
    visitrec_begin();
    discipline_init();

    ui_diag_create();
    scr_main_create();
    /* Load AFTER the room layer exists: restored messes are real objects and
     * need somewhere to be drawn. */
    persist_load();
    /* RAW state, before any catch-up or live tick can touch it. This is the
     * value that must match what was saved. */
    persist_print_state("RAW LOADED");
    scr_main_show();
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

    /* Offline catch-up. Only ever runs against a TRUSTED clock: with the OS
     * flag set we have no idea whether the elapsed time is real, and
     * simulating three days that may not have happened is worse than
     * simulating nothing. */
    {
        char tb[32];
        rtc_format(rtc_now(), tb, sizeof(tb));
        Serial.printf("\nRTC: %s   time now: %s\n",
                      rtc_health_name(rtc_health()), tb);

        if (rtc_trusted() && pet_get()->last_sim_ts) {
            sim_report_t rep;
            sim_catch_up(pet_get()->last_sim_ts, rtc_now(), &rep);
            sim_print_report();
            const char *g = sim_return_greeting(&rep);
            if (g) ui_bubble_say(BUBBLE_T0_CRITICAL, g);
            /* NO DREAM HOOK HERE ANY MORE, on purpose. An absence that
             * covered a night is just a sleep period that closed while
             * nobody was looking, and care_advance() already accumulated and
             * closed it during the catch-up above - recording the dream in
             * pending_dream. The first live tick tells it, deferred, so it
             * queues behind this greeting. One rule, one place. */
        } else if (!rtc_trusted()) {
            Serial.println("Clock not trusted - no catch-up. Set the time on Settings.");
        }
        persist_print_state("SIMULATED");
        evolve_check_announce();   /* one-time post-offline reveal */
        pet_mutable()->last_sim_ts = rtc_now();
        if (rtc_trusted()) persist_save(true);   /* anchor the new baseline */
    }

    Serial.println();
    Serial.printf("PET: form %s, animation at %d fps\n",
                  forms_name(ui_pet_get_form()), 1000 / T_ANIM_MS);
    Serial.println("Tap the pet to make it react. Press ? for the command list.");
    Serial.printf("MENU: BOOT short press or the on-screen Menu handle. "
                  "Transition = %s\n", menu_transition_name());

    lv_timer_create(imu_timer_cb,  1000 / DIAG_IMU_PRINT_HZ, NULL);
    lv_timer_create(boot_timer_cb, 20,    NULL);
    lv_timer_create(cons_timer_cb, 30,    NULL);
    lv_timer_create(anim_timer_cb, T_ANIM_MS, NULL);
    lv_timer_create(sim_timer_cb,  T_SIM_MS,  NULL);
    lv_timer_create(save_timer_cb, T_SAVE_MS, NULL);
    lv_timer_create(heartbeat_cb,  10000, NULL);
}

void loop()
{
    lv_timer_handler();
    delay(5);
}
