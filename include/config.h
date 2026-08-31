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

/* --- Time / visit [SPEC section 6] --------------------------------------
 * VISIT_LENGTH_DAYS (a flat 21) IS GONE. The visit is no longer scheduled,
 * it is EARNED: the Visitor leaves somewhere between day 9 and day 16 and
 * where it lands inside that window is a function of how the stay actually
 * went. See farewell.h for the whole model - this block is only the dial. */
#define VISIT_DEPART_MIN_DAY     9.0f
#define VISIT_DEPART_MAX_DAY    16.0f

/* How far the projected date may MOVE at one re-evaluation. 0.15 days is
 * 3.6 hours: enough that a day of good care is felt, far too little for one
 * burger to buy a day. */
#define VISIT_DEPART_MAX_DRIFT   0.15f

/* Re-evaluation cadence, in SIMULATED Visitor-hours - so an offline gap gets
 * exactly as many re-evaluations as the same time spent awake would, because
 * both run through care_advance(). At 6 h that is 4 per day, so the date can
 * travel at most 0.6 days/day: a full traverse of the 7-day window takes
 * about twelve days of consistently changed behaviour. */
#define VISIT_DEPART_EVAL_HOURS  6.0f

/* Within this long of the projected date, the date FREEZES. Nothing after
 * the lock can move it, so the goodbye a child has been told about is the
 * goodbye they get. */
#define VISIT_DEPART_LOCK_HOURS  36

/* Foreshadowing starts here, and this MUST be smaller than the lock window
 * above - a hint that could still be retracted is worse than no hint. */
#define VISIT_HINT_HOURS         30
#define VISIT_HINT_MIN_GAP_MS    (25UL * 60UL * 1000UL)  /* gentle, not nagging */

/* A recalculation may never place departure in the past or inside this
 * window. Care that collapses on the last day shortens nothing retroactively;
 * there is always at least this much notice left. */
#define VISIT_DEPART_MIN_NOTICE_H 6.0f

/* How long a pending departure waits before it outranks everything else that
 * could delay it. Past this a game, an open menu or a mischief window no
 * longer buy time - but SLEEP STILL DOES, always. This is a priority
 * escalation, not an override: the farewell is witnessed or it does not
 * happen. See farewell.h §5. */
#define VISIT_HOLD_MAX_HOURS     48

/* Stay quality -> departure date. A straight linear map overshoots at the
 * bottom and undershoots at the top (measured against the reference seeds),
 * so the blended 0..100 stay score is passed through a logistic before it is
 * spread across the 9..16 window. k is the steepness, x0 the centre; both
 * were solved from the three existing care seeds and are checked by the
 * calibration table the $ console command prints. */
#define VISIT_STAY_CURVE_K       8.0f
#define VISIT_STAY_CURVE_X0      0.628f

/* Fraction of the ADULT stretch (adult start -> departure) at which the
 * improvement-only form re-check happens. Replaces a hardcoded "day 18",
 * which was a fixed fraction of the old fixed 21-day visit and is meaningless
 * against a variable one. */
#define VISIT_RECHECK_FRACTION   0.5f

#define SIM_ELAPSED_CAP_SEC     (72L * 3600L)   /* stat decay cap */
#define SIM_CHUNK_SEC           (15L * 60L)     /* 15-minute chunks */
/* RESCALED 24 h -> 12 h for the 1/3/6-day lifecycle.
 *
 * The 24 h value was chosen when Baby lasted 3 days, so one half-life was a
 * third of the stage. With a 1-day Baby stage it became the WHOLE stage:
 * Baby care could barely move the accumulators before Kid selection read
 * them, so the first stage had almost no say in its own outcome.
 *
 * At 12 h one half-life is half the Baby stage, restoring roughly the old
 * ratio, and the accumulator closes 75% of its gap within that stage.
 * Recomputed guarantees (both testable):
 *   two days of perfect care from a bottomed-out 20 -> 95   (was 80)
 *   one bad day from a perfect 100                  -> 40   (was 60)
 * So recovery got faster AND one bad day bites harder - but 40 is back above
 * 70 after twelve hours of good care, so nothing is locked. 8 h was rejected
 * as too twitchy: it drops a perfect Visitor to 30 for a single bad day. */
#define ACCUM_HALFLIFE_HOURS    12.0f

/* --- Evolution boundary handling [MILESTONE 8] --------------------------
 * A composite that lands within EVO_EPS of a threshold must NOT let
 * floating-point residue pick a major branch. Observed in testing: a BA of
 * -0.028 printed as "-0.0" and silently routed a Visitor to Mischief Kid.
 *
 * Inside the neutral zone the documented tie-break applies: BENEFIT OF THE
 * DOUBT - the kinder branch wins. A Visitor sitting exactly on the line has
 * not done anything wrong, and a child should not be handed the worse
 * outcome by a rounding error. Deterministic for the same saved history. */
#define EVO_EPS                 0.75f

/* Evolution presentation. Short on purpose - about 2.6 s total. A long
 * transformation is charming once and tedious every time after. */
#define EVO_WALK_MS             600     /* move to centre                  */
#define EVO_SHRINK_MS           500     /* pull in and pulse               */
#define EVO_FLASH_MS            260     /* white silhouette flash          */
#define EVO_GROW_MS             620     /* grow into the new form          */
#define EVO_CHEER_MS            700     /* hop + sparkles + bubble         */

/* --- Discipline [MILESTONE 8] ------------------------------------------
 * Discipline is contextual: it responds to a real misbehaviour window, and
 * cannot be farmed by pressing the button. */
#define DISC_WINDOW_MS          45000UL  /* how long an opportunity lasts   */
#define DISC_GAIN               8.0f     /* moderate, per correct telling-off */
#define DISC_UNFAIR_HAPPY_LOSS  4.0f
#define DISC_IGNORED_LOSS       3.0f     /* when a window expires unused    */
#define DISC_IGNORED_MISCHIEF   6        /* tendency rises when ignored     */
#define DISC_ANNOY_DECAY_MS     120000UL /* unfair-discipline annoyance     */

/* Spontaneous mischief. Frequency scales with the hidden tendency and falls
 * as discipline rises, so a well-raised Visitor genuinely misbehaves less.
 *
 * SUPERSEDED BY PHASE 9.5. These three are the pre-9.5 values and are no
 * longer read by anything: the roll is now stage-weighted and history-aware
 * (MISCHIEF_CHECK_FAST_MS / MISCHIEF_BASE_PCT_9_5 / the MISCHIEF_GAP_* pair,
 * all in the 9.5 block at the end of this file). They are kept only so the
 * old numbers are still legible next to the new ones - delete them once
 * nobody needs to compare. */
#define MISCHIEF_CHECK_MS       30000UL  /* superseded: was the roll cadence */
#define MISCHIEF_BASE_PCT       6        /* superseded: was the flat % chance */
#define MISCHIEF_MIN_GAP_MS     180000UL /* superseded: was a FIXED 3 min gap */

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

/* --- The egg [MILESTONE 9] ---------------------------------------------
 * A new Visitor arrives as an egg and hatches on a timer the player starts.
 * The shell colour is purely cosmetic and is deliberately NOT fed into any
 * accumulator - a child should be able to hope for a favourite colour
 * without it secretly deciding how their Visitor turns out. */
#define EGG_HATCH_SEC           300     /* five minutes                    */
#define EGG_PALETTE_COUNT       6

/* Egg selector geometry lives in the PHASE 9.5 block at the end of this
 * file: the pre-hatch screen gained a gender row, so the whole layout was
 * re-derived rather than squeezed. Only the shell's own spot count stays
 * here, because it is a property of the drawing and not of the picker. */
#define EGG_DOTS                7

/* Freshly hatched: hungry and wanting attention, but in a clean room. The
 * first thing a child should want to do is feed it and play with it - not
 * clean up after a mess it did not make. */
#define EGG_HATCH_HUNGER        10.0f
#define EGG_HATCH_HAPPINESS     10.0f
#define EGG_HATCH_CLEANLINESS   100.0f
#define STAGE_BABY              1
#define STAGE_KID               2
#define STAGE_TEEN              3
#define STAGE_ADULT             4

/* FRACTIONAL days, compared against pet_age_days(). Whole-day granularity
 * would mean a Visitor hatched at 18:00 becomes a Kid at midnight rather
 * than 24 hours later - tolerable at a 3-day Baby stage, badly wrong at a
 * 1-day one. */
#define STAGE_DAY_KID           1.0f
#define STAGE_DAY_TEEN          3.0f
#define STAGE_DAY_ADULT         6.0f

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
/* The VIRTUAL room light is a scene-level dim, not the panel backlight.
 * bsp_set_brightness() dims the whole device including menus, which made the
 * Food and Care pages unreadable at night - the room being dark must not make
 * the controls dark. Physical brightness stays available as a real device
 * setting later. */
#define ROOM_DIM_OPA            170     /* black overlay alpha, 0..255     */
#define LIGHTS_TOO_BRIGHT_MS    45000UL  /* min gap between complaints      */

/* If the Visitor is awake while the clock says it should be asleep and the
 * normal transition has not taken hold within this long, it is put in bed
 * unconditionally. A missed bedtime should never survive - whatever the
 * cause: a long action, a suspended tick, a boot in the middle of the night. */
#define SLEEP_CATCHUP_MS        4000UL

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
/* Span narrowed 20..120 -> 35..95 alongside the slower cake weight. Measured
 * on device: 5 cakes moves 45.0 -> 47.4 g and 20 cakes -> 53.0 g. Over the
 * old 100 g span that was ~1% and ~3% of body width - five cakes correctly
 * invisible, but twenty barely readable either. Over 60 g the same grams give
 * ~1.6% and ~5.3%, so the top end shows while five cakes still does not. */
#define PET_WEIGHT_MIN_G        35.0f
#define PET_WEIGHT_MAX_G        95.0f
#define PET_WEIGHT_START_G      45.0f

/* Growth spurt: on each stage transition, excess weight above the new
 * stage's baseline is pulled partway back, as if the Visitor grew into it.
 * Never a full reset - an overeating history should still be able to produce
 * a Chonky Adult, and the junk accumulator is untouched by this. */
#define GROWTH_SPURT_FRACTION   0.45f
#define STAGE_BASELINE_BABY_G   45.0f
#define STAGE_BASELINE_KID_G    52.0f
#define STAGE_BASELINE_TEEN_G   60.0f
#define STAGE_BASELINE_ADULT_G  68.0f

/* FOOD. Cake is the only item that cannot be refused, which is what makes it
 * the route to overfeeding - and it pays badly in hunger, so filling up on
 * cake is a poor strategy without ever being blocked. */
#define FOOD_FULL_PCT           90.0f   /* at/above: burger+fruit refused    */
#define FOOD_PARTIAL_PCT        75.0f   /* at/above: burger half-eaten       */

#define BURGER_HUNGER           28.0f
#define BURGER_WEIGHT_G         0.5f
/* APPLE IS A SNACK, NOT A MEAL. It tops up a little and is accepted whenever
 * there is any room at all - it never triggers the burger's partial-eat or
 * fullness refusal, because "too full for a whole burger" and "too full for
 * one apple" are not the same thing. */
/* Refuse at 99+, not at exactly 100: hunger is a float that decays every
 * tick, so it sits at 99.97 and an == 100 test would essentially never fire.
 * The player sees "100" either way. */
#define FRUIT_FULL_PCT          99.0f
#define FRUIT_HUNGER            5.0f
#define FRUIT_WEIGHT_G          0.05f
#define CAKE_HUNGER             8.0f    /* modest, by design                 */
/* RETUNED: cake was 3.0 g, which made five cakes visibly fatten the Visitor.
 * At 0.8 g five cakes move body width by well under 2%, and it takes roughly
 * 15-20 before chubbiness reads clearly. The junk/nutrition accumulator that
 * feeds Evolution is UNCHANGED - visible weight and care history are related
 * but separate systems. */
#define CAKE_WEIGHT_G           0.8f
#define CAKE_HAPPY              6.0f

#define FOOD_PARTIAL_FRACTION   0.5f    /* how much of a burger gets eaten   */
#define FOOD_REFUSE_DROP_PCT    100     /* % chance a refused item is dropped */

/* BATHROOM. Need climbs continuously; the holding pose and warnings start at
 * URGENT, and the accident only lands after a grace period on top of that -
 * so there is always a visible warning window before anything goes wrong. */
/* PER-STAGE, RANDOMISED, measured in Visitor-AWAKE hours to urgent.
 * Replaces a single 22.0/hour constant that gave every stage the same 3.2 h
 * to urgent - already inside the Baby band and FASTER than the requested
 * Adult band, so the flat rate was not what made it feel infrequent.
 *
 * A fresh target is drawn from the stage's range at the start of every cycle
 * and PERSISTED, so the timing never feels like clockwork and a reboot does
 * not reroll it. */
#define BATH_HOURS_BABY_MIN     2.5f
#define BATH_HOURS_BABY_MAX     3.5f
#define BATH_HOURS_KID_MIN      3.0f
#define BATH_HOURS_KID_MAX      4.0f
#define BATH_HOURS_TEEN_MIN     3.5f
#define BATH_HOURS_TEEN_MAX     5.0f
#define BATH_HOURS_ADULT_MIN    4.0f
#define BATH_HOURS_ADULT_MAX    6.0f

/* Sleep multiplier. The rate used to be applied identically asleep and
 * awake - unlike hunger, happiness, energy, cleanliness and weight, which
 * all branch on ctx->asleep. An 11-hour night therefore overflowed the meter
 * twice and guaranteed an overnight accident. At 0.25 a night adds roughly
 * 60% of a cycle instead. */
#define BATHROOM_SLEEP_RATE     0.25f

/* Visual warning tiers. Escalation is mostly POSE, not speech - more
 * frequent potty events must not mean more nagging. */
#define BATH_TIER_SUBTLE_PCT    60.0f
#define BATH_TIER_OBVIOUS_PCT   80.0f
#define BATH_TIER_URGENT_PCT    95.0f
#define BATH_WARN_OBVIOUS_MS    45000UL
#define BATH_WARN_URGENT_MS     20000UL
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

/* =========================================================================
 * PHASE 9.5 - personality, dreams, identity, refinement
 * ====================================================================== */

/* --- Age copy -----------------------------------------------------------
 * 1 real day = 1 Visitor year, and the CHILD-FACING copy now says so. The
 * diagnostics keep printing day numbers - a developer needs the raw figure -
 * but "day 6" on the Stats page told a five-year-old nothing, where "6 years
 * old" is immediately legible and matches the stage boundaries exactly
 * (Kid at 1, Teen at 3, Adult at 6). There is no second clock here: the
 * displayed age IS days_alive, relabelled. */
#define AGE_YEARS_PER_DAY       1

/* --- Identity: gender ---------------------------------------------------
 * PRESENTATION ONLY. Gender must never reach an accumulator, a form choice,
 * a care rate or a discipline rule - the same standing rule the egg colour
 * has lived under since Phase 9. It exists so a child can say "she" or "he"
 * about their Visitor, and for nothing else. Surprise resolves ONCE at START
 * and is persisted immediately, exactly like the colour. */
#define GENDER_BOY              0
#define GENDER_GIRL             1
#define GENDER_SURPRISE         2       /* the CHOICE only; never resolved  */

/* Palette index EGG_PALETTE_COUNT is the rainbow "Surprise" shell. It is a
 * display value, never a stored colour: egg_color always holds a REAL
 * palette index, and the rainbow is selected from egg_choice so the resolved
 * colour stays hidden right up to the hatch. */
#define EGG_PAL_RAINBOW         EGG_PALETTE_COUNT

/* Pre-hatch reveal, then the first thing the Visitor ever says. Deliberately
 * sequential and not overlapping: the reveal owns the screen, the greeting
 * follows it. */
#define HATCH_REVEAL_MS         3200UL
#define HATCH_GREET_DELAY_MS    900UL   /* after the reveal clears          */

/* --- Deferred reactions --------------------------------------------------
 * A reaction the player is SUPPOSED to see must not be spoken to a closed
 * menu. Queue it, then show it once the pet screen is back - and start its
 * timer THEN, not when it was queued. Bounded hard: three slots, oldest
 * dropped, and anything that has waited longer than the hold is discarded
 * rather than surfacing minutes late and out of context. */
#define BUBBLE_DEFER_SLOTS      3
#define BUBBLE_DEFER_TEXT_MAX   80
#define BUBBLE_DEFER_HOLD_MS    120000UL

/* The no-repeat history OWNS ITS STRINGS. It used to keep bare pointers,
 * which was safe only while every line was a string literal - and the
 * deferred queue broke that assumption the moment it existed. See
 * recently_said() in ui_bubble.cpp for the failure it caused. 5 x 96 bytes
 * of static RAM is a trivial price for removing the whole class. */
#define BUBBLE_RECENT_TEXT_MAX  96

/* --- Dreams -------------------------------------------------------------
 * Flavour only. A dream never touches a stat, an accumulator or the
 * evolution path. Night sleep always produces one; a Baby's afternoon nap
 * sometimes does, which is what makes the nap dream feel like a find rather
 * than a fixture. */
#define DREAM_NAP_CHANCE_PCT    35
#define DREAM_KEEP              3       /* recent dreams retained for the Journal */

/* MINIMUM RECORDED SLEEP before a period is eligible to have been dreamt in.
 * Both are durations of sleep ACTUALLY ACCUMULATED, not "is the clock inside
 * a sleep window" - see the sleep-period block in care.cpp for why that
 * distinction is the whole mechanism. Configurable; nothing hardcodes them. */
#define DREAM_MIN_NIGHT_SEC     (2L * 3600L)    /* a night: two hours       */
#define DREAM_MIN_NAP_SEC       (20L * 60L)     /* a Baby's nap: 20 minutes */

/* Why these thresholds. A 96-second reboot at half past eight in the evening
 * used to produce a dream, so switching the device off and on again got you
 * one every time and the mechanic stopped being a morning surprise. Two
 * hours is roughly a quarter of a night: long enough that the Visitor
 * plausibly dreamt, short enough that a genuine evening absence still
 * counts. Twenty minutes is the same judgement for an afternoon nap, which
 * only runs for an hour in the first place. */

/* --- Lights reactions ---------------------------------------------------
 * Switching the room light off while the Visitor is AWAKE does not put it to
 * sleep - sleep is the clock's job and always has been. It gets a joke
 * instead. Cooldowns stop a child flicking the switch from turning it into a
 * chatterbox. */
#define LIGHTS_REACT_GAP_MS     20000UL
#define LIGHTS_BACK_CHANCE_PCT  70      /* the lights-ON line is optional   */

/* --- Old-mess comments --------------------------------------------------
 * Only once a poop has aged past STINK_AFTER_MS and grown its stink lines,
 * so the comment always refers to something visible on screen. LONG cooldown
 * on purpose: this is a gag, and a gag repeated every minute is nagging. */
#define POOP_COMMENT_GAP_MS     300000UL   /* five minutes, minimum         */
#define POOP_COMMENT_CHANCE_PCT 45         /* rolled once per gap           */

/* --- Discipline opportunities [PHASE 9.5] -------------------------------
 * Spontaneous mischief is now STAGE-WEIGHTED and HISTORY-AWARE.
 *
 * Baseline frequency, by design: Kid most, Baby second, Teen third, Adult
 * least. A Kid is the stage where correcting behaviour is the game; a Baby
 * is naughty in a harmless, funny way; a Teen and an Adult have mostly
 * learned better. The weights below are percentages applied to the base roll.
 *
 * BABIES ARE NO LONGER BLAMELESS. They used to be excluded entirely, which
 * removed the whole mechanic from the first day of every visit. What a Baby
 * does is deliberately limited to the harmless kinds - dropped food, a small
 * deliberate mess, bedtime silliness - and never anything that reads as a
 * real fault.
 *
 * Unavoidable needs are STILL never misconduct. Nothing here changes that
 * rule: hunger, tiredness, dirt, a genuine bathroom accident and refusing
 * food when actually full remain outside discipline entirely. */
#define MISCHIEF_W_BABY         85      /* second most                      */
#define MISCHIEF_W_KID          100     /* most                             */
#define MISCHIEF_W_TEEN         62      /* third                            */
#define MISCHIEF_W_ADULT        40      /* least                            */

#define MISCHIEF_CHECK_FAST_MS  15000UL /* roll cadence (was 30 s)          */
#define MISCHIEF_BASE_PCT_9_5   14      /* % per roll before weighting      */

/* Randomised gap between opportunities. A naturally mischievous Visitor in
 * active play lands roughly one every 1-3 minutes; a calm one is far rarer
 * because the roll itself keeps failing, not because the gap is longer. */
#define MISCHIEF_GAP_MIN_MS     60000UL
#define MISCHIEF_GAP_MAX_MS     180000UL

/* SETTLING-IN HOLDS. A Visitor that has just arrived must not misbehave
 * before it has finished arriving.
 *
 * Observed on hardware: a Baby hatched, the "It's a boy!" reveal appeared,
 * and a mischief bubble ("Look what I made!") took the screen DURING it -
 * pushing the Visitor's own first words into the deferred queue. The first
 * thing that ever happened to that Visitor was a discipline window, before
 * it had said hello. discipline_init() leaves s_last_mis_ms at 0, so the
 * very first roll after any fresh start is eligible.
 *
 * The hatch hold covers the reveal, the greeting and a moment to breathe.
 * The boot hold is shorter and exists for the same reason at a smaller
 * scale: the return greeting and any post-absence dream should not be
 * elbowed aside fifteen seconds after switch-on. */
#define MISCHIEF_SETTLE_HATCH_MS 120000UL
#define MISCHIEF_SETTLE_BOOT_MS   60000UL

/* LEARNED BEHAVIOUR. A rolling EMA over how discipline windows were actually
 * resolved: corrected pulls it toward 0, ignored pulls it toward 100. It
 * starts neutral, decays with every new event, and is NEVER locked - a
 * Visitor raised badly as a Kid can still be brought round as a Teen, and a
 * perfectly-raised Kid whose Teen years are ignored will drift back. That
 * recoverability is the whole point; a permanent penalty for a bad first day
 * would be the wrong lesson on a child's device. */
#define LEARN_START             50.0f
#define LEARN_ALPHA             0.22f   /* per resolved window              */
#define LEARN_WEIGHT_PCT        60      /* how much it moves the roll, %    */

/* --- Pre-hatch selector geometry [PHASE 9.5] ----------------------------
 * The layout now carries a gender row as well as seven colours, so it was
 * re-derived from scratch rather than squeezed. Every gap below is stated as
 * arithmetic so it can be CHECKED rather than eyeballed - the last selector
 * bug here was a -4 px overlap that looked fine:
 *
 *   egg preview   root y = EGG_ROOT_Y (-6); the shell spans  30 .. 148
 *   colour row 0  156 .. 208      (4 swatches, 80 x 52)
 *   colour row 1  214 .. 266      (3 swatches: teal, yellow, Surprise)
 *   gap                            266 -> 292 = 26 px
 *   gender row    292 .. 344      (3 buttons, 108 x 52)
 *   DEAD SPACE                     344 -> 370 = 26 px   (spec wants >= 20-24)
 *   START         370 .. 442      (72 tall, 6 px clear of the 448 panel)
 *
 * Row 0 is 4*80 + 3*10 = 350 px in a 368 px panel: 9 px a side.
 * The gender row is 3*108 + 2*12 = 348 px: 10 px a side.
 *
 * A touch that BEGINS on a selector can never end on START, because the two
 * hitboxes are 26 px apart and LVGL delivers CLICKED to the object the press
 * started on. Both conditions matter; the gap alone was not the old bug. */
#define EGG_ROOT_Y              (-6)
#define EGG_SW_W                80
#define EGG_SW_H                52
#define EGG_SW_GAP_X            10
#define EGG_SW_ROW0_Y           156
#define EGG_SW_ROW1_Y           214
#define EGG_GENDER_W            108
#define EGG_GENDER_H            52
#define EGG_GENDER_GAP_X        12
#define EGG_GENDER_Y            292
#define EGG_START_Y             370
#define EGG_START_H             72

/* The two gaps, named so a build can assert them. */
#define EGG_GENDER_GAP_ABOVE    (EGG_GENDER_Y - (EGG_SW_ROW1_Y + EGG_SW_H))
#define EGG_START_DEAD_BAND     (EGG_START_Y - (EGG_GENDER_Y + EGG_GENDER_H))

/* --- Hatch-countdown layout [PRESENTATION ONLY] -------------------------
 * EGG_ROOT_Y (-6) exists because the SELECTOR screen needs the room: two
 * colour rows, a gender row and START fill everything below 156. The moment
 * START is pressed all of that goes away, and the shell is left stranded at
 * the top of a panel that is now empty from 148 all the way down to 448 -
 * three quarters of the screen, unused, with the egg crowded under the HUD.
 *
 * So the countdown DROPS the egg to the Visitor's own home spot. The target
 * is PET_HOME_Y rather than a freshly-computed "middle" on purpose: it is
 * the exact position the Baby occupies when the shell opens, so the hatch is
 * a shell opening around a Visitor already standing where it belongs. There
 * is nothing left to jump.
 *
 *   HUD row          0 .. ~30    (mood dot left, menu handle right)
 *   countdown label  142 .. 166
 *   gap                          166 -> 186 = 20 px
 *   egg shell        186 .. 304  (root 150 + 36 in-box, 118 tall)
 *
 * The label+egg group spans 142..304 in a 448 panel: 142 px clear above,
 * 144 below - centred to within 2 px. Both margins are asserted in
 * scr_main.cpp rather than eyeballed, for the same reason every other gap in
 * this block is.
 *
 * The shell's in-box offset is NOT a free parameter: layout_egg() places the
 * shell at ey = PET_BOX_PX - EGG_SHELL_H - 6, and the two names below are
 * asserted to agree with it. */
#define EGG_SHELL_H             118
#define EGG_SHELL_TOP_IN_BOX    (PET_BOX_PX - EGG_SHELL_H - 6)
#define EGG_HATCH_ROOT_Y        PET_HOME_Y
#define EGG_HATCH_LBL_Y         142
#define EGG_HATCH_LBL_H         24
#define EGG_DROP_MS             700     /* the glide down; timing only      */

#define EGG_HATCH_SHELL_TOP     (EGG_HATCH_ROOT_Y + EGG_SHELL_TOP_IN_BOX)
#define EGG_HATCH_LBL_GAP       (EGG_HATCH_SHELL_TOP - (EGG_HATCH_LBL_Y + EGG_HATCH_LBL_H))
#define EGG_HATCH_TOP_CLEAR     (EGG_HATCH_LBL_Y)
#define EGG_HATCH_BOTTOM_CLEAR  (BSP_LCD_H - (EGG_HATCH_SHELL_TOP + EGG_SHELL_H))
