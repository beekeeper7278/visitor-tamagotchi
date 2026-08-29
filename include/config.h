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
#define VISITOR_VERSION      "0.3.0-phase3+4"
#define VISITOR_PHASE        3

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

/* --- Life stages [MILESTONE 5] -----------------------------------------
 * Day boundaries are the ones already agreed: Teen at day 7, Adult at
 * day 13, visit ends at day 21. Kid at day 3 fills the gap between Baby and
 * Teen. Egg/Newborn is the pre-hatch state: it lasts until a trusted clock
 * establishes a hatch timestamp, because without a real date there is no
 * such thing as "day 3".
 *
 * PHASE 5+6 SETS THE STAGE ONLY. Which Kid / Teen / Adult FORM the Visitor
 * becomes is Phase 8 evolution work and is deliberately not decided here. */
#define STAGE_EGG               0
#define STAGE_BABY              1
#define STAGE_KID               2
#define STAGE_TEEN              3
#define STAGE_ADULT             4

#define STAGE_DAY_KID           3
#define STAGE_DAY_TEEN          7
#define STAGE_DAY_ADULT         13

/* --- Sleep window [MILESTONE 5] ----------------------------------------
 * Local wall-clock hours. Night sleep is the long one; the Baby also naps in
 * the afternoon, and that nap is reconstructed on boot like any other sleep
 * period. */
#define SLEEP_START_HOUR        20
#define SLEEP_END_HOUR          7
#define NAP_START_HOUR          13
#define NAP_END_HOUR            14
#define NAP_MAX_STAGE           STAGE_BABY   /* babies nap; older stages do not */

/* Sleep presentation. The bed sits centre-low; the Visitor walks to it and
 * settles rather than the state changing invisibly. */
#define BED_CX                  (BSP_LCD_W / 2)
#define BED_CY                  300
#define SLEEP_SPOT_X            (BED_CX - PET_BOX_PX / 2)
#define SLEEP_SPOT_Y            (BED_CY - 118)

/* Lights OFF dims the PANEL, using the verified brightness control rather
 * than a translucent overlay - a real dim costs less power on AMOLED and
 * looks like night instead of like fog. Never overwrites the daytime value. */
#define LIGHTS_OFF_BRIGHTNESS   0x18
#define LIGHTS_TOO_BRIGHT_MS    45000UL  /* min gap between complaints      */

/* --- Offline catch-up caps [MILESTONE 6] -------------------------------
 * TIME IS NEVER CAPPED. Age, days alive, stage timing and visit duration
 * always advance by the full elapsed interval. What is capped is how much
 * DAMAGE one absence may do, each stat independently, so a week in a drawer
 * is a story rather than a dead pet.
 *
 * These are per-absence totals, not rates. */
#define OFFLINE_HUNGER_MAX_DROP    55.0f
#define OFFLINE_HAPPY_MAX_DROP     30.0f   /* gentle: never full -> zero    */
#define OFFLINE_CLEAN_MAX_DROP     40.0f
#define OFFLINE_MAX_ACCIDENTS      1       /* one unattended accident, max  */
/* Where the need is parked once the offline accident cap is spent. Leaving
 * it at 100 meant the live tick fired a SECOND accident within seconds of
 * boot - technically one offline accident, but indistinguishable from two to
 * anyone actually holding the device. Parking it urgent-but-not-overflowing
 * returns a Visitor doing the potty dance with the normal grace period left
 * to react. */
#define OFFLINE_BATHROOM_PARK_PCT  95.0f
#define OFFLINE_LONG_ABSENCE_SEC   (36L * 3600L)  /* "WHERE HAVE YOU BEEN?!" */
#define OFFLINE_MIN_NOTICE_SEC     (10L * 60L)    /* below this, say nothing */

/* Weight deliberately has NO offline rule: absence alone must not change it.
 * See docs/PHASE5-6-OFFLINE-REQUIREMENTS.md. */

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

/* 2D roaming area. The pet explores in both axes rather than sliding along a
 * rail. The bounds are chosen to keep the 160x160 box clear of the HUD strip
 * at the top and off the floor where messes sit, so wandering can never
 * collide with UI or hide a mess behind the pet. */
#define PET_ROAM_X_MIN          PET_WALK_MARGIN_PX
#define PET_ROAM_X_MAX          (BSP_LCD_W - PET_BOX_PX - PET_WALK_MARGIN_PX)
#define PET_ROAM_Y_MIN          64      /* below the HUD icons              */
#define PET_ROAM_Y_MAX          232     /* above the floor / mess strip     */

/* Floor line for room messes - deliberately independent of the pet's
 * position now that the pet moves vertically. */
#define ROOM_FLOOR_Y            (BSP_LCD_H - 62)

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

/* --- Weight, food, bathroom, messes [MILESTONE 3B] ----------------------
 * ALL tunable from here by request - nothing below is buried in logic.
 *
 * WEIGHT: the renderer scales body_w by +/-20% across weight_norm 0..1, and
 * it relayouts every frame - so a big jump in weight_g would be instantly and
 * obviously visible. The 20..120 g span is therefore chosen so that a single
 * cake (+3 g) moves weight_norm by 0.03, i.e. body width by ~0.6% - about one
 * pixel. Getting visibly chonky takes many cakes, which is the requirement. */
#define PET_WEIGHT_MIN_G        20.0f
#define PET_WEIGHT_MAX_G        120.0f
#define PET_WEIGHT_START_G      45.0f

/* FOOD. Cake is the only item that cannot be refused, which is what makes it
 * the route to overfeeding - and it pays badly in hunger, so filling up on
 * cake is a poor strategy without ever being blocked. */
#define FOOD_FULL_PCT           90.0f   /* at/above: burger+fruit refused    */
#define FOOD_PARTIAL_PCT        75.0f   /* at/above: burger half-eaten       */

#define BURGER_HUNGER           28.0f
#define BURGER_WEIGHT_G         2.5f
#define FRUIT_HUNGER            16.0f
#define FRUIT_WEIGHT_G          0.6f
#define CAKE_HUNGER             8.0f    /* modest, by design                 */
#define CAKE_WEIGHT_G           3.0f
#define CAKE_HAPPY              6.0f

#define FOOD_PARTIAL_FRACTION   0.5f    /* how much of a burger gets eaten   */
#define FOOD_REFUSE_DROP_PCT    100     /* % chance a refused item is dropped */

/* BATHROOM. Need climbs continuously; the holding pose and warnings start at
 * URGENT, and the accident only lands after a grace period on top of that -
 * so there is always a visible warning window before anything goes wrong. */
#define RATE_BATHROOM_PER_HOUR  22.0f
#define BATHROOM_URGENT_PCT     70.0f   /* holding pose + warning bubbles    */
#define BATHROOM_GRACE_MS       90000UL /* after urgent, before an accident  */
#define BATHROOM_WARN_MS        20000UL /* min gap between warning bubbles   */
#define BATHROOM_MIN_TO_GO_PCT  15.0f   /* below this, the button is a no-op */
#define BATHROOM_RUNOFF_MS      900     /* run to off-screen                 */
#define BATHROOM_AWAY_MS        2000    /* "about 2 seconds"                 */
#define BATHROOM_RETURN_MS      900
/* Hard upper bound on the whole run-off/away/return sequence. Generous
 * against the ~3.8 s the phases actually need; it exists only so a missing
 * pet can never be permanent. */
#define BATHROOM_TOTAL_MAX_MS   8000
#define BATHROOM_ACCIDENT_CLEAN 12.0f   /* one-off cleanliness hit           */

/* MESSES. The rule that matters: one forgotten item must NOT wreck
 * cleanliness. Each mess waits out a grace period, then drains slowly, and
 * each mess stops draining once it has taken its own capped total. So a mess
 * left for a week costs the same as one left for an hour past its cap -
 * annoying and visible, never catastrophic. */
#define MESS_MAX                4       /* matches the section 9 sprite pool */
#define MESS_GRACE_MS           60000UL /* nothing happens for a minute      */
#define MESS_FOOD_DRAIN_PER_H   2.5f
#define MESS_POOP_DRAIN_PER_H   6.0f    /* accidents cost more than food     */
#define MESS_FOOD_DRAIN_CAP     15.0f   /* total this mess can ever take     */
#define MESS_POOP_DRAIN_CAP     30.0f

#define CLEAN_RECOVERY_PCT      18.0f   /* modest, per the requirement       */
#define CLEAN_EFFECT_MS         700     /* sparkle burst on cleaning         */
#define PUFF_COUNT              7       /* cleaning puffs                    */
#define PUFF_MS                 620
#define CLEAN_STEP_MS           220     /* gap between messes vanishing      */
#define CLEAN_SEQ_MAX_MS        2000    /* whole sequence stays 1-2 s        */
#define STINK_AFTER_MS          180000UL/* stink lines once it has sat a while*/

/* Food presentation. The item is shown falling to the Visitor and then
 * either eaten, half-eaten or refused with a head shake - so the outcome is
 * something you watch rather than something you read in a bubble. */
#define FOOD_DROP_MS            700     /* falls from above to the pet       */
#define FOOD_EAT_MS             1200    /* chewing                           */
#define FOOD_CHEW_MS            150     /* mouth open/close period [SPEC 8]  */
#define FOOD_REFUSE_MS          800     /* head shake "no"                   */
#define FOOD_SHAKES             3       /* how many times the head turns     */
#define FOOD_SHAKE_PX           7

/* Where food lands. Randomised within a reachable zone rather than dropped
 * onto the Visitor: the point is that the Visitor goes TO its dinner. The
 * zone is inset from every edge and sits above the floor mess strip, so a
 * dropped item is always fully visible and never collides with the HUD. */
#define FOOD_ZONE_X_MIN         30
#define FOOD_ZONE_X_MAX         (BSP_LCD_W - 70)
#define FOOD_ZONE_Y_MIN         190
#define FOOD_ZONE_Y_MAX         320
#define FOOD_APPROACH_MAX_MS    2500    /* safety bound on the walk         */

/* --- Pager / menu [SPEC section 4] --------------------------------------
 * Only the CURRENT page exists as LVGL objects. On swipe the neighbour is
 * built at x = +/-368, both animate, the old one is deleted. Index is mod 6,
 * so the 6->1 wrap is free - no sentinel clones, no scroll-snap fighting. */
#define PAGE_COUNT              6

/* Transition style. Phase 1 measured 19.7 fps end-to-end, below the ~20 the
 * design wanted for a full-width slide, so all three are implemented behind
 * this one setting and compared on the actual panel.
 *   0 = SLIDE  368 px horizontal, the original design
 *   1 = FADE   cross-fade, cheaper in frames but still a full redraw
 *   2 = CUT    instant swap + a page-dot flash; cheapest by far */
#define PAGE_TR_SLIDE           0
#define PAGE_TR_FADE            1
#define PAGE_TR_CUT             2
#define PAGE_TRANSITION         PAGE_TR_CUT

#define PAGE_SLIDE_MS           250
#define PAGE_FADE_MS            120
#define PAGE_CUT_FLASH_MS       180     /* dot flash, so a cut still reads  */

#define DOT_SIZE_PX             8
#define DOT_GAP_PX              10
#define DOT_BOTTOM_MARGIN       14

/* --- Diagnostics --------------------------------------------------------- */
#define DIAG_FPS_SAMPLE_FRAMES  30
#define DIAG_IMU_PRINT_HZ       10
