#pragma once
/* ===========================================================================
 * config.h - game + system tunables
 *
 * Values tagged [GUESS] are my inventions and are expected to be tuned after
 * hardware testing. Values tagged [SPEC] come from the design document and
 * changing them changes documented behaviour.
 * ======================================================================== */

#include <stdint.h>
#include "board_pins.h"   /* BSP_LCD_W for the pet home position */

/* --- Build identity ---------------------------------------------------- */
#define VISITOR_VERSION      "0.2.0-phase2"
#define VISITOR_PHASE        2

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

/* --- Pet rendering + animation [SPEC section 8] -------------------------
 * The pet occupies a 160x160 box on the 368x448 screen. Animation runs at
 * 10 fps on t_anim; that is a design decision, not a limitation - a blink
 * redraws ~24x24 and LVGL's dirty-rect tracking keeps the flush cost far
 * below a full frame. */
#define PET_BOX_PX              160
#define PET_HOME_X              ((BSP_LCD_W - PET_BOX_PX) / 2)
#define PET_HOME_Y              150
#define PET_WALK_MARGIN_PX      16      /* keeps the box fully on screen    */

#define ANIM_BREATHE_PERIOD_MS  2000    /* body_h +/-3 px sine              */
#define ANIM_BREATHE_AMP_PX     3
#define ANIM_BLINK_MS           120     /* eye height -> 2 px               */
#define ANIM_BLINK_MIN_MS       3000    /* random every 3-7 s               */
#define ANIM_BLINK_MAX_MS       7000
#define ANIM_WALK_MS            1500    /* x lerp to target                 */
#define ANIM_WALK_BOB_PX        4
#define ANIM_REACT_MS           600     /* body jitter +/-6 px              */
#define ANIM_REACT_JITTER_PX    6
#define ANIM_HOP_MS             180     /* two hops for the happy anim      */
#define ANIM_HOP_HEIGHT_PX      14
#define ANIM_SAD_DROP_PX        6

/* Autonomous idle behaviour [GUESS - tune by watching it]. A pet that only
 * moves when poked reads as a picture rather than a creature, so idle
 * wandering and idle chatter are behaviours, not test hooks. */
/* Idle wander interval. TUNED ON HARDWARE 2026-08-28: 8-20 s read as too
 * infrequent on the real panel - the pet looked inert between walks - so it
 * was tightened to 4-9 s. Walk speed and duration are deliberately NOT
 * changed; only how often a walk is started.
 *
 * The interval is measured from the END of the previous walk, not its start:
 * the walk completion path calls ui_pet_play(PET_ANIM_IDLE), which re-arms
 * the timer at that instant. That is what stops walks chaining back-to-back
 * when the interval is short - and at 4 s it is short enough to matter. */
#define IDLE_WALK_MIN_MS        4000
#define IDLE_WALK_MAX_MS        9000
#define IDLE_CHATTER_MS         15000   /* ATTEMPT interval; the bubble
                                         * cooldowns decide what actually
                                         * gets through, which is the point */

/* Live modifiers applied over the form at draw time [SPEC section 8].
 * Weight scales body_w by +/-20%, so the same form reads fed vs starved. */
#define PET_WEIGHT_SCALE_PCT    20

/* --- Speech bubbles [SPEC section 10] -----------------------------------
 * Four tiers, one bubble at a time, higher tier preempts. These numbers are
 * SPEC, not guesses - changing one changes documented behaviour. */
#define BUBBLE_FADE_MS          150     /* preempt cross-fade               */
#define BUBBLE_BASE_MS          2500    /* duration = base + per-char       */
#define BUBBLE_PER_CHAR_MS      40
#define BUBBLE_MAX_MS           5000
#define BUBBLE_GLOBAL_CD_MS     8000
#define BUBBLE_T0_CD_MS         20000   /* critical need                    */
#define BUBBLE_T1_CD_MS         3000    /* reaction to input                */
#define BUBBLE_T2_CD_MS         45000   /* mood flavour                     */
#define BUBBLE_T3_CD_MS         90000   /* idle chatter                     */
#define BUBBLE_NO_REPEAT_DEPTH  5       /* no string repeat within last 5   */

/* --- Bubble layout ------------------------------------------------------
 * The box is LV_SIZE_CONTENT in both axes, so it hugs the label: width comes
 * from the measured text width clamped to [MIN, MAX], and height grows on
 * its own as lines wrap. TEXT_MAX_W is the wrap width - the label's width,
 * NOT the box's - so the padding and border below are added on top of it.
 *
 * Sizing rationale: 232 px of text at montserrat_20 holds roughly 22-24
 * characters per line, which keeps normal dialogue to 1-3 lines. The full
 * box is then 232 + 2*12 pad + 2*2 border = 260 px, comfortably inside the
 * 368 px panel with room to sit off-centre when the pet is near an edge. */
#define BUBBLE_PAD_PX           12
#define BUBBLE_BORDER_PX        2
#define BUBBLE_TEXT_MAX_W       232     /* wrap width for the label         */
#define BUBBLE_TEXT_MIN_W       40      /* stops "Hi!" becoming a sliver    */
#define BUBBLE_BOX_MAX_W        (BUBBLE_TEXT_MAX_W + 2 * (BUBBLE_PAD_PX + BUBBLE_BORDER_PX))
#define BUBBLE_SCREEN_MARGIN    8       /* hard clamp against every edge    */
#define BUBBLE_TAIL_GAP_PX      12      /* gap between bubble and pet       */

/* --- Diagnostics --------------------------------------------------------- */
#define DIAG_FPS_SAMPLE_FRAMES  30
#define DIAG_IMU_PRINT_HZ       10
