/* farewell - end of visit. See farewell.h. */

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "forms.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "gamerec.h"
#include "evolve.h"
#include "journal.h"
#include "visitrec.h"
#include "persist.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "scr_main.h"
#include "audio.h"
#include "menu.h"
#include "games.h"
#include "sim.h"
#include "strings.h"
#include "farewell.h"

static lv_obj_t *s_scr;
static bool      s_active;
static char      s_note[FAREWELL_MAX];

bool farewell_active(void) { return s_active; }

/* ==========================================================================
 * THE VISIT MODEL
 *
 * All of the reasoning is in farewell.h. What lives here is the arithmetic,
 * and the one rule that is easy to lose in it: every path that changes the
 * projected date goes through eval_departure(), so the drift clamp, the
 * notice floor and the lock cannot be bypassed by adding a caller.
 * ======================================================================== */

/* Simulated hours since the last re-evaluation. Deliberately NOT persisted:
 * losing at most VISIT_DEPART_EVAL_HOURS of accumulation across a power cycle
 * is worth less than another schema field, and it cannot move the date past
 * the drift clamp either way. */
static float s_eval_h;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Blended stay score, 0..1.
 *
 * cs carries four fifths of it because cs IS the care score - its own weights
 * already sum to 1.0 over happiness, feeding, cleanliness, sleep and
 * engagement. ba contributes the remaining fifth, rescaled from its -100..100
 * range, so discipline and ignored requests colour the outcome without being
 * able to dominate it: a Visitor cannot be sent home early purely for being
 * mischievous. */
float visit_stay01(void)
{
    const evo_scores_t s = evolve_scores();
    return clamp01((0.80f * s.cs + 0.20f * (s.ba + 100.0f) * 0.5f) / 100.0f);
}

/* The logistic that maps stay quality onto the window. See farewell.h §2 for
 * why this is not a straight line. */
static float stay_curve(float x)
{
    return 1.0f / (1.0f + expf(-VISIT_STAY_CURVE_K * (x - VISIT_STAY_CURVE_X0)));
}

float visit_target_day(void)
{
    const float y = stay_curve(visit_stay01());
    return VISIT_DEPART_MIN_DAY +
           y * (VISIT_DEPART_MAX_DAY - VISIT_DEPART_MIN_DAY);
}

float visit_depart_day(void) { return pet_get()->depart_day; }
bool  visit_locked(void)     { return pet_get()->depart_locked != 0; }

float visit_hours_left(void)
{
    const pet_state_t *p = pet_get();
    if (!p->depart_day) return 0.0f;
    return (p->depart_day - pet_age_days()) * 24.0f;
}

float visit_recheck_day(void)
{
    const pet_state_t *p = pet_get();
    const float end = p->depart_day ? p->depart_day : VISIT_DEPART_MAX_DAY;
    if (end <= STAGE_DAY_ADULT) return STAGE_DAY_ADULT;
    return STAGE_DAY_ADULT + (end - STAGE_DAY_ADULT) * VISIT_RECHECK_FRACTION;
}

void visit_reset(void)
{
    pet_state_t *p = pet_mutable();
    p->depart_day    = 0.0f;
    p->depart_due_ts = 0;
    p->depart_locked = 0;
    p->stay_band     = 0;
    s_eval_h         = 0.0f;
}

/* ONE re-evaluation. Every stability rule lives here and nowhere else. */
static void eval_departure(void)
{
    pet_state_t *p = pet_mutable();

    /* No calendar age means no meaningful projection. An egg has not
     * started its visit. */
    if (!p->hatch_ts || !rtc_trusted()) return;

    /* RULE 3, first, because it short-circuits the rest: once locked, the
     * date is the date. Nothing below may touch it. */
    if (p->depart_locked) return;

    const float age    = pet_age_days();
    const float target = visit_target_day();

    if (p->depart_day <= 0.0f) {
        /* FIRST projection: the MIDDLE of the window, not the score-derived
         * target.
         *
         * A newborn's accumulators are not earned, they are authored - the
         * egg hatches deliberately hungry and wanting attention
         * (EGG_HATCH_HUNGER / EGG_HATCH_HAPPINESS are both 10), which reads
         * to evolve_scores() as cs 30.7 and anchors the whole visit near the
         * day-9 floor before the child has done anything at all. Measured on
         * hardware: the first projection came out at 9.66.
         *
         * Starting neutral makes the two directions symmetric - neglect
         * shortens the visit, care lengthens it - and costs nothing, because
         * the drift budget is ample: 0.15 days per 6 simulated hours is 0.6
         * days/day, so the full 7-day window can be traversed in about twelve
         * days and BOTH ends are reached well before the 36 h lock. */
        p->depart_day = (VISIT_DEPART_MIN_DAY + VISIT_DEPART_MAX_DAY) * 0.5f;
        Serial.printf("VISIT: first projection seeded neutral at day %.2f "
                      "(target is %.2f and it will drift there)\n",
                      (double)p->depart_day, (double)target);
    } else {
        /* RULE 1: drift clamp. */
        float d = target - p->depart_day;
        if (d >  VISIT_DEPART_MAX_DRIFT) d =  VISIT_DEPART_MAX_DRIFT;
        if (d < -VISIT_DEPART_MAX_DRIFT) d = -VISIT_DEPART_MAX_DRIFT;
        p->depart_day += d;
    }

    /* The window is a hard boundary in both directions. The poor-care band
     * sits on the day-9 floor and this is where it CLAMPS: the logistic tail
     * already compresses very poor and poor to within a few hours of each
     * other, and this line is what guarantees neither can go under 9. That
     * is deliberate, not an arithmetic accident - see visit_report(). */
    if (p->depart_day < VISIT_DEPART_MIN_DAY) p->depart_day = VISIT_DEPART_MIN_DAY;
    if (p->depart_day > VISIT_DEPART_MAX_DAY) p->depart_day = VISIT_DEPART_MAX_DAY;

    /* RULE 2: never retroactive. A recalculation may not put departure in
     * the past or inside the notice window. Still bounded by the ceiling
     * afterwards - if the floor would push it past day 16 the visit is
     * simply over, and the due check handles that. */
    const float floor_day = age + VISIT_DEPART_MIN_NOTICE_H / 24.0f;
    if (p->depart_day < floor_day) p->depart_day = floor_day;
    if (p->depart_day > VISIT_DEPART_MAX_DAY) p->depart_day = VISIT_DEPART_MAX_DAY;

    /* RULE 3 is NOT applied here - see visit_check_lock(). */
}

/* RULE 3: lock on approach, permanently.
 *
 * Deliberately NOT part of the re-evaluation cadence. The lock is a DEADLINE
 * test, not a re-projection: tying it to the 6-hour cadence meant it could
 * engage up to six hours late, which eats into the 6-hour margin that is
 * supposed to separate VISIT_HINT_HOURS (30) from VISIT_DEPART_LOCK_HOURS
 * (36) and is what guarantees a hint can never precede the lock. Measured on
 * hardware at exactly 36.0 hours remaining, the lock had not engaged. It is
 * a float compare against a value that already exists, so running it at 1 Hz
 * costs nothing. */
static void visit_check_lock(void)
{
    pet_state_t *p = pet_mutable();
    if (p->depart_locked || p->depart_day <= 0.0f) return;
    if (!p->hatch_ts || !rtc_trusted()) return;

    const float left_h = (p->depart_day - pet_age_days()) * 24.0f;
    if (left_h > (float)VISIT_DEPART_LOCK_HOURS) return;

    p->depart_locked = 1;
    Serial.printf("VISIT: departure LOCKED at day %.2f (%.1f h away)\n",
                  (double)p->depart_day, (double)left_h);
    persist_mark_dirty("departure locked");
}

void visit_advance(float hours)
{
    if (hours <= 0.0f) return;

    /* The FIRST projection does not wait for the cadence. Otherwise a freshly
     * hatched Visitor has depart_day 0 - i.e. no answer at all - for the first
     * six simulated hours, which makes every report and every test read as
     * "not projected yet" during exactly the window someone is most likely to
     * be looking. There is nothing to drift-limit on a value that has never
     * existed, so evaluating early is free. */
    const pet_state_t *p = pet_get();
    if (p->depart_day <= 0.0f && p->hatch_ts) { eval_departure(); return; }

    s_eval_h += hours;
    if (s_eval_h < VISIT_DEPART_EVAL_HOURS) return;

    /* Consume whole cadence steps rather than zeroing, so a single long
     * offline chunk gets every re-evaluation it earned - and therefore every
     * drift step it earned. Zeroing here would let one absence skip past the
     * clamp by collapsing many evaluations into one. */
    while (s_eval_h >= VISIT_DEPART_EVAL_HOURS) {
        s_eval_h -= VISIT_DEPART_EVAL_HOURS;
        eval_departure();
    }
}

/* --- foreshadowing -------------------------------------------------------
 * Gated on the LOCK, not on the raw date, so a hint can never be given and
 * then withdrawn. VISIT_HINT_HOURS (30) is inside VISIT_DEPART_LOCK_HOURS
 * (36), which is what makes that guarantee hold.
 *
 * Tone: wistful, never ominous, and never a countdown. A five-year-old should
 * feel that something is coming, not be handed a deadline. */
static const char *HINT[] = {
    "I had a dream about home last night.",
    "I can see my star from here!",
    "I'm going to remember this place.",
    "My people will be looking for me soon.",
};

void visit_tick(void)
{
    /* The lock first, and unconditionally: it must engage whether or not the
     * Visitor is asleep, in a game or behind the menu. Only the HINT below is
     * gated on someone being there to hear it. */
    visit_check_lock();

    const pet_state_t *p = pet_get();
    if (s_active || !p->depart_locked || p->asleep) return;
    if (games_active() || menu_is_open()) return;

    const float h = visit_hours_left();
    if (h > (float)VISIT_HINT_HOURS || h <= 0.0f) return;

    static uint32_t s_last_hint;
    const uint32_t now = millis();
    if (s_last_hint && now - s_last_hint < VISIT_HINT_MIN_GAP_MS) return;
    s_last_hint = now;
    Serial.printf("VISIT: hint (%.1f h left of a %d h window)\n",
                  (double)h, VISIT_HINT_HOURS);

    ui_bubble_say(BUBBLE_T2_MOOD,
                  HINT[(uint8_t)(now / 1000) % (sizeof(HINT) / sizeof(HINT[0]))]);
}

/* --- is it time, and can anyone SEE it? ---------------------------------- */

bool farewell_due(void)
{
    if (s_active) return false;

    pet_state_t *p = pet_mutable();
    if (!p->hatch_ts || !rtc_trusted()) return false;
    if (p->depart_day <= 0.0f) return false;
    if (pet_age_days() < p->depart_day) return false;

    /* The moment has arrived. ARM it once, and stamp WHEN from the RTC -
     * not from millis(), which resets on every boot and would restart the
     * wait cap each time the device came up. */
    if (!p->depart_due_ts) {
        p->depart_due_ts = rtc_now();
        Serial.printf("VISIT: departure due at day %.2f - armed, waiting to "
                      "be witnessed\n", (double)p->depart_day);
        persist_mark_dirty("departure armed");
        persist_save(true);
    }

    /* SLEEP IS ABSOLUTE, AND THE CAP DOES NOT OVERRIDE IT.
     *
     * This gate used to sit BELOW the cap, so a departure that had been
     * pending for 48 hours fired regardless - which could play the goodbye
     * to a sleeping Visitor in a dark room at 3 am. That is precisely the
     * failure the witnessing rule exists to prevent, and a cap that defeats
     * it is not a safety valve, it is a hole. A farewell is ALWAYS witnessed.
     *
     * `care_sleep_due()` is checked as well as the `asleep` flag, not instead
     * of it: at boot, sim_catch_up() clears `asleep` ("you are here now") and
     * care_tick() only re-asserts it on the next tick - and can defer even
     * that if a scripted sequence is in flight. Testing the CLOCK as well
     * closes that window, so reconnecting at 3 am cannot slip a farewell
     * through before bedtime is re-established. */
    if (p->asleep || care_sleep_due()) return false;

    /* Past VISIT_HOLD_MAX_HOURS the departure becomes top priority: the
     * Visitor is awake and the device is on, so nothing else gets to delay it
     * any further. A game or an open menu is a reason to wait a moment, never
     * a reason to extend a visit that should already have ended - and
     * farewell_begin() closes both. The note acknowledges the wait. */
    const uint32_t now = rtc_now();
    const bool capped = now > p->depart_due_ts &&
        (now - p->depart_due_ts) >= (uint32_t)VISIT_HOLD_MAX_HOURS * 3600UL;
    if (capped) {
        Serial.printf("VISIT: departure held %lu h (cap %d) and the Visitor is "
                      "awake - going now\n",
                      (unsigned long)((now - p->depart_due_ts) / 3600UL),
                      VISIT_HOLD_MAX_HOURS);
        return true;
    }

    /* Under the cap, WITNESSED also means nothing else owns the screen. Note
     * there is no screen-blank state to consult - the IDLE_* constants in
     * config.h are reserved but the idle dim/off behaviour is not
     * implemented, so "device on" is the strongest availability signal that
     * actually exists. */
    if (games_active()) return false;
    if (menu_is_open()) return false;
    return true;
}

/* --- the note ------------------------------------------------------------ */

void farewell_compose(char *out, size_t len)
{
    const pet_state_t *p = pet_get();
    const gamerec_t   *g = gamerec_get();

    /* What went WELL is chosen first and always leads, because the note has
     * to open warmly whatever kind of visit it was. */
    const char *praise;
    if (p->care_fed >= 70.0f)        praise = "You kept me so well fed";
    else if (p->care_clean >= 70.0f) praise = "You kept my room lovely and tidy";
    else if (g->total_games >= 5)    praise = "We played SO many games";
    else if (p->care_happy >= 60.0f) praise = "You always cheered me up";
    else                             praise = "You were there when it counted";

    /* One memorable thing, drawn from what actually happened. */
    char memory[80];
    if (p->cakes_eaten >= 3)
        snprintf(memory, sizeof(memory), "I'm still thinking about all that cake");
    else if (g->maze_best_ms && g->maze_best_ms < 30000)
        snprintf(memory, sizeof(memory), "that maze run was UNREAL");
    else if (p->accidents > 0)
        snprintf(memory, sizeof(memory), "we do not talk about the accident");
    else if (g->total_games > 0)
        snprintf(memory, sizeof(memory), "%s was my favourite", gamerec_name(gamerec_favorite()));
    else
        snprintf(memory, sizeof(memory), "the quiet days were nice too");

    /* Improvement ONLY when genuinely warranted, and phrased gently. */
    const char *improve = "";
    if (p->care_clean < 40.0f)      improve = " Maybe we could tidy up a bit faster next time...";
    else if (p->care_fed < 40.0f)   improve = " Maybe a few more snacks next time?";
    else if (p->lights_forgotten > 2) improve = " And the light! You kept leaving the light on!";

    /* Personality colours the sign-off, not the facts. */
    const char *sign;
    if (p->trait_a == PERS_MISCHIEVOUS || p->trait_b == PERS_MISCHIEVOUS)
        sign = "You tried to keep me out of trouble. You almost succeeded!";
    else if (p->trait_a == PERS_DRAMATIC || p->trait_b == PERS_DRAMATIC)
        sign = "This is the MOST emotional I have ever been. Goodbye!";
    else if (p->trait_a == PERS_SHY || p->trait_b == PERS_SHY)
        sign = "I'll miss you. A lot. Even if I didn't say so much.";
    else
        sign = "I'm going to miss you!";

    /* THE OPENER CARRIES THE STAY LENGTH. It replaces the old fixed "Earth
     * was amazing!" rather than being appended, which is both better writing
     * and what keeps the note inside FAREWELL_MAX.
     *
     * The tone rule is the hard one and it applies most here: a short visit
     * is a consequence of the experience, never an accusation. The short
     * opener is warm and a bit silly, and says nothing about whose fault
     * anything was. Nothing in this function may resemble "you were bad so
     * I'm leaving." */
    const float span = VISIT_DEPART_MAX_DAY - VISIT_DEPART_MIN_DAY;
    const float pos  = (pet_age_days() - VISIT_DEPART_MIN_DAY) / span;
    const char *opener;
    if (pos >= 0.6667f)
        opener = "I had such a good time here I didn't want to leave!";
    else if (pos >= 0.3333f)
        opener = "I had a lot of fun visiting Earth!";
    else
        opener = "Well, that went fast - Earth is BUSY!";

    /* Only when the departure moment actually had to wait to be witnessed.
     * Kept short and cheerful; it explains a delay, it does not apologise
     * for one. */
    const char *held = "";
    if (p->depart_due_ts && rtc_trusted()) {
        const uint32_t now = rtc_now();
        if (now > p->depart_due_ts && (now - p->depart_due_ts) >= 3600UL)
            held = " (I waited up so I could say this properly!)";
    }

    const int n = snprintf(out, len, "%s %s, and %s.%s %s%s",
                           opener, praise, memory, improve, sign, held);
    if (n < 0 || (size_t)n >= len)
        Serial.printf("FAREWELL: note TRUNCATED - wanted %d bytes, have %u\n",
                      n, (unsigned)len);
}

/* --- console report ------------------------------------------------------
 * Modelled on evolve_explain(): the selection must be EXPLAINABLE, not just
 * correct. Prints the live numbers and then re-derives the whole calibration
 * table from the reference care seeds, so the curve can be checked against
 * the spec's figures without seeding anything and losing the live history. */

/* Pure recomputation of the pipeline for a hypothetical (cs, ba). Used only
 * by the table below - the live path never calls it. */
static void calib_row(const char *name, float cs, float ba, float want_lo,
                      float want_hi)
{
    const float stay01 = clamp01((0.80f * cs + 0.20f * (ba + 100.0f) * 0.5f) / 100.0f);
    const float linear = VISIT_DEPART_MIN_DAY +
                         stay01 * (VISIT_DEPART_MAX_DAY - VISIT_DEPART_MIN_DAY);
    const float curved = VISIT_DEPART_MIN_DAY +
                         stay_curve(stay01) * (VISIT_DEPART_MAX_DAY - VISIT_DEPART_MIN_DAY);
    Serial.printf("   %-10s %6.1f %6.1f   %5.3f   %5.2f   %5.2f   %.0f-%.0f  %s\n",
                  name, (double)cs, (double)ba, (double)stay01,
                  (double)linear, (double)curved, (double)want_lo, (double)want_hi,
                  (curved >= want_lo && curved <= want_hi) ? "ok" : "OUT OF BAND");
}

void visit_report(void)
{
    const pet_state_t *p = pet_get();
    const evo_scores_t s = evolve_scores();
    const float age = pet_age_days();

    Serial.println();
    Serial.println("=== VISIT / DEPARTURE =====================================");
    Serial.printf("  age            : %.3f days (%s)\n", (double)age,
                  pet_stage_name(p->stage));
    Serial.printf("  cs %.1f   ba %.1f   ia %.1f   engage %.1f\n",
                  (double)s.cs, (double)s.ba, (double)s.ia, (double)s.engage);
    Serial.printf("  stay01         : %.3f  -> curve %.3f\n",
                  (double)visit_stay01(), (double)stay_curve(visit_stay01()));
    Serial.printf("  target day     : %.2f   (what the model wants right now)\n",
                  (double)visit_target_day());
    Serial.printf("  IN FORCE       : day %.2f   %s\n", (double)p->depart_day,
                  p->depart_day <= 0.0f ? "(not projected yet)"
                                        : (p->depart_locked ? "LOCKED" : "drifting"));
    Serial.printf("  hours left     : %.1f   (lock at %d h, hints from %d h)\n",
                  (double)visit_hours_left(), VISIT_DEPART_LOCK_HOURS,
                  VISIT_HINT_HOURS);
    Serial.printf("  drift cap      : %.2f days per %.0f simulated hours"
                  "   (%.1f h since last eval)\n",
                  (double)VISIT_DEPART_MAX_DRIFT, (double)VISIT_DEPART_EVAL_HOURS,
                  (double)s_eval_h);
    Serial.printf("  adult re-check : day %.2f\n", (double)visit_recheck_day());
    if (p->depart_due_ts) {
        const uint32_t now = rtc_now();
        const float waited = (now > p->depart_due_ts)
                           ? (float)(now - p->depart_due_ts) / 3600.0f : 0.0f;
        Serial.printf("  ARMED          : due %lu, held %.1f h of a %d h cap\n",
                      (unsigned long)p->depart_due_ts, (double)waited,
                      VISIT_HOLD_MAX_HOURS);
    } else {
        Serial.println("  ARMED          : no - departure has not fallen due");
    }
    Serial.println();
    Serial.println("  CALIBRATION (recomputed, live history untouched)");
    Serial.println("   seed           cs     ba   stay01  linear  curved   want");
    calib_row("_ POOR",      25.4f, -40.0f,  9.0f, 10.0f);
    calib_row("= MID",       59.1f,   3.0f, 11.0f, 12.0f);
    calib_row("& GOOD",      75.9f,  24.3f, 13.0f, 14.0f);
    calib_row("+ EXCELLENT", 91.3f,  39.2f, 15.0f, 16.0f);
    calib_row("(floor)",      0.0f,-100.0f,  9.0f, 10.0f);
    calib_row("(ceiling)",  100.0f, 100.0f, 15.0f, 16.0f);
    Serial.println("   'linear' is the spec's naive map, kept for comparison:");
    Serial.println("   it lands excellent but runs poor and average a day high,");
    Serial.println("   which is why the logistic exists. The floor row shows");
    Serial.println("   the day-9 CLAMP: very-poor and poor differ by hours, so");
    Serial.println("   the shared outcome is a clamp, not an accident.");
    Serial.println("-----------------------------------------------------------");
}

/* --- the screen ---------------------------------------------------------- */

static void goodbye_cb(lv_event_t *e);

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_scr);
    lv_label_set_text(t, "Goodbye!");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFD24A), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14);

    const pet_state_t *p = pet_get();
    char sub[96];
    /* AGE, in the units the rest of the child-facing UI now uses: 1 real day
     * = 1 Visitor year. "12.4 days on Earth" was a duration masquerading as
     * an age on the one screen where the two are numerically identical. */
    snprintf(sub, sizeof(sub), "%s  -  %d years old",
             forms_long_name(p->form_id), (int)pet_age_days());
    lv_obj_t *sl = lv_label_create(s_scr);
    lv_label_set_text(sl, sub);
    lv_obj_set_style_text_color(sl, lv_color_hex(0x8890A0), 0);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, 50);

    /* The note scrolls, because a good one will not always fit. */
    lv_obj_t *box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, BSP_LCD_W - 32, 250);
    lv_obj_set_pos(box, 16, 80);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x14141C), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);

    lv_obj_t *nl = lv_label_create(box);
    lv_label_set_text(nl, s_note);
    lv_label_set_long_mode(nl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(nl, BSP_LCD_W - 32 - 24);
    lv_obj_set_style_text_font(nl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(nl, lv_color_hex(0xE8E8E8), 0);

    lv_obj_t *b = lv_obj_create(s_scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BSP_LCD_W - 64, 74);
    lv_obj_set_pos(b, 32, 352);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x60D0A0), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, goodbye_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, "Say goodbye");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(0x101018), 0);
    lv_obj_center(bl);
}

void farewell_begin(void)
{
    /* The one cue that should land before anything is read. */
    audio_play(SND_FAREWELL);
    if (s_active) return;
    s_active = true;

    ui_bubble_set_suppressed(true);
    /* A held reaction from before the goodbye must never surface DURING or
     * AFTER it - "Who turned out the sun?" landing on top of a farewell note
     * would be the worst possible timing for a joke. */
    ui_bubble_drop_deferred();
    ui_pet_set_wander(false);
    /* Nothing else may still own the screen once the goodbye starts. Past the
     * hold cap farewell_due() no longer waits for these, so it is here that a
     * game in flight gets shut down rather than drawn under the note. */
    if (games_active()) games_force_exit();
    if (menu_is_open()) menu_close();

    farewell_compose(s_note, sizeof(s_note));
    Serial.println();
    Serial.println("=== FAREWELL ==============================================");
    Serial.printf("  %s\n", s_note);
    Serial.println("-----------------------------------------------------------");

    build_screen();
    lv_scr_load(s_scr);
}

/* Only AFTER the goodbye is acknowledged does anything get archived or
 * reset. Nothing is destroyed while the note is still on screen. */
static void goodbye_do(void)
{
    visitrec_archive(s_note);

    pet_init();                 /* fresh Visitor, new personality */
    visit_reset();              /* the next visit earns its own date */
    journal_clear();
    care_reset();
    gamerec_reset();            /* records are per-Visitor; history is kept */

    /* The next Visitor arrives as an EGG, not a Baby: pet_init() has already
     * set stage EGG and rolled a shell colour. Hatching happens when the
     * player starts the timer. */
    pet_state_t *p = pet_mutable();
    p->hatch_ts = 0;
    if (rtc_trusted()) p->last_sim_ts = rtc_now();
    persist_save(true);

    s_active = false;
    lv_obj_t *dead = s_scr;
    s_scr = nullptr;
    scr_main_show();
    if (dead) lv_obj_del(dead);

    ui_bubble_set_suppressed(false);
    ui_pet_set_egg(true, p->egg_color);
    Serial.println("FAREWELL: archived - a new egg is waiting");
}

static void goodbye_cb(lv_event_t *e) { (void)e; goodbye_do(); }
void farewell_acknowledge(void) { if (s_active) goodbye_do(); }
