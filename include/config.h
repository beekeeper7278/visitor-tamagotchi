#pragma once
/* ===========================================================================
 * config.h - game + system tunables
 *
 * Values tagged [GUESS] are my inventions and are expected to be tuned after
 * hardware testing. Values tagged [SPEC] come from the design document and
 * changing them changes documented behaviour.
 * ======================================================================== */

#include <stdint.h>

/* --- Build identity ---------------------------------------------------- */
#define VISITOR_VERSION      "0.1.0-phase1"
#define VISITOR_PHASE        1

/* --- LVGL display buffer ------------------------------------------------
 * [SPEC] ~60-line single buffer in INTERNAL SRAM. Do not move to PSRAM
 * without a measured justification. 368 * 60 * 2 = 44160 bytes. */
#define LV_DRAW_BUF_LINES    60

/* --- LVGL timer periods (ms) [SPEC section 1] -------------------------- */
#define T_SIM_MS             1000
#define T_ANIM_MS            100
#define T_UI_MS              250
#define T_IMU_MS             50
#define T_RTC_MS             30000
#define T_IDLE_MS            1000
#define T_SAVE_MS            600000

/* --- Pet stat rates, units per hour [SPEC section 2] --------------------
 * Not used in Phase 1; defined here so the table lives in exactly one place
 * and Phase 4/5 cannot silently diverge from the design doc. */
#define RATE_HUNGER_AWAKE       (-6.0f)
#define RATE_HUNGER_ASLEEP      (-2.0f)
#define RATE_HAPPY_AWAKE        (-2.5f)
#define RATE_HAPPY_ASLEEP       (-0.5f)
#define RATE_ENERGY_AWAKE       (-5.0f)
#define RATE_ENERGY_SLEEP_DARK  (12.0f)
#define RATE_ENERGY_SLEEP_LIT   (6.0f)
#define RATE_CLEAN_AWAKE        (-2.0f)
#define RATE_CLEAN_ASLEEP       (-1.0f)
#define RATE_DISCIPLINE         (-0.5f)
#define RATE_WEIGHT_AWAKE       (-0.15f)
#define RATE_WEIGHT_ASLEEP      (-0.05f)

#define HAPPINESS_DECAY_FLOOR   15.0f   /* [SPEC] never brutal */

/* --- Time / visit [SPEC section 6] -------------------------------------- */
#define VISIT_LENGTH_DAYS       21
#define SIM_ELAPSED_CAP_SEC     (72L * 3600L)   /* stat decay cap */
#define SIM_CHUNK_SEC           (15L * 60L)     /* 15-minute chunks */
#define ACCUM_HALFLIFE_HOURS    24.0f

/* RTC plausibility bounds [SPEC section 6] */
#define RTC_MIN_VALID_TS        1704067200UL    /* 2024-01-01 00:00:00 */
#define RTC_MAX_VALID_TS        2682374400UL    /* 2055-01-01 00:00:00 */
#define RTC_BACKWARD_SLACK_SEC  120L
#define RTC_FORWARD_JUMP_SEC    (45L * 86400L)

/* --- Storage [SPEC section 5] ------------------------------------------- */
#define NVS_NAMESPACE           "visitor"
#define NVS_KEY_SAVE            "save"
#define NVS_VISIT_RECORDS       5
#define SAVE_MIN_INTERVAL_MS    60000UL         /* hard floor between writes */

/* --- Idle / burn-in [GUESS section 10] ---------------------------------- */
#define IDLE_DIM1_MS            30000UL         /* -> 60% brightness */
#define IDLE_DIM2_MS            120000UL        /* -> 25% brightness */
#define IDLE_OFF_MS             300000UL        /* -> display off */
#define IDLE_LIGHTSLEEP_MS      900000UL        /* -> light sleep */
#define BRIGHT_FULL             0xD0            /* matches CO5300 init value */
#define BRIGHT_DIM1             0x80
#define BRIGHT_DIM2             0x30
#define BURNIN_SHIFT_PERIOD_MS  90000UL
#define BURNIN_SHIFT_PX         4

/* --- Input [GUESS section 4] -------------------------------------------- */
#define TAP_MAX_MS              300
#define TAP_MAX_TRAVEL_PX       20
#define SWIPE_MIN_TRAVEL_PX     45
#define SWIPE_AXIS_RATIO        2               /* |dx| > 2*|dy| */

/* BOOT button (GPIO0, verified). Short press toggles the menu in Phase 3.
 * A "short" press is bounded so that holding BOOT - which is also how the
 * ROM bootloader is entered - never reads as a menu toggle. */
#define BOOT_DEBOUNCE_MS        30              /* [GUESS] */
#define BOOT_SHORT_PRESS_MAX_MS 800             /* [GUESS] longer = ignored */

/* --- Diagnostics --------------------------------------------------------- */
#define DIAG_FPS_SAMPLE_FRAMES  30
#define DIAG_IMU_PRINT_HZ       10
