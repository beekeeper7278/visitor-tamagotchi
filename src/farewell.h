#pragma once
/* ===========================================================================
 * farewell - end of visit, and the VARIABLE VISIT MODEL  [MILESTONE 9 +
 *            gameplay pacing pass]
 *
 * The note is built LOCALLY from deterministic templates plus the real visit
 * history. No network, no model - it must work on a device in a drawer.
 *
 * Tone rule, and it is the hard one: warm, funny, never guilt-heavy. A child
 * who left the Visitor filthy for a week should still want to read this. The
 * "could improve" clause is included ONLY when genuinely warranted, and is
 * phrased as a suggestion between friends rather than a report card. A
 * shorter visit is a CONSEQUENCE of the experience, never an accusation.
 *
 * ---------------------------------------------------------------------------
 * THE VISIT IS NO LONGER SCHEDULED, IT IS EARNED
 *
 * VISIT_LENGTH_DAYS (a flat 21) is gone. The Visitor departs somewhere in
 * [VISIT_DEPART_MIN_DAY, VISIT_DEPART_MAX_DAY] and where it lands is a
 * continuous function of how the stay actually went. Day 12.4 is a perfectly
 * ordinary answer; the four care profiles in the spec are expected OUTCOMES
 * for reference histories, not buckets.
 *
 * 1. STAY QUALITY REUSES THE EVOLUTION ACCUMULATORS.
 *    visit_stay01() is derived from evolve_scores(), which is already an EMA
 *    over the one shared care_advance() path and is already correct offline.
 *    There is deliberately NO second set of decayed counters: a parallel
 *    scoring system would drift from the first and would double the surface
 *    the offline path has to keep right.
 *
 * 2. THE MAP IS A LOGISTIC, NOT A LINE.
 *    A straight line from the blended score into 9..16 overshoots the bottom
 *    of the range and undershoots the top - measured against the three
 *    reference seeds, poor landed at 10.8 (wanted 9-10) and excellent at 15.1
 *    (wanted 15-16). VISIT_STAY_CURVE_K / _X0 were solved from those seeds.
 *    The $ console command prints the calibration table so the numbers can be
 *    checked rather than believed.
 *
 * 3. IT IS RE-EVALUATED FOR THE WHOLE VISIT, not fixed at the Adult
 *    transition, so late care still moves the date. The cadence is
 *    VISIT_DEPART_EVAL_HOURS of SIMULATED time, driven from care_advance() -
 *    which means an absence gets exactly as many re-evaluations as the same
 *    span spent awake.
 *
 * 4. FOUR STABILITY RULES, all of them load-bearing:
 *      - drift is clamped to VISIT_DEPART_MAX_DRIFT per re-evaluation, so one
 *        burger cannot buy a day;
 *      - the date may never move into the past or inside
 *        VISIT_DEPART_MIN_NOTICE_H, so collapsing care on the last afternoon
 *        cannot retroactively shorten a visit;
 *      - inside VISIT_DEPART_LOCK_HOURS the date FREEZES permanently;
 *      - foreshadowing starts at VISIT_HINT_HOURS, which is inside the lock,
 *        so a hint can never start and then be taken back.
 *
 * 5. DEPARTURE MUST ALWAYS BE WITNESSED. NO EXCEPTIONS, INCLUDING THE CAP.
 *    A variable date can fall at 3 am or while the device is in a drawer.
 *    Firing then would play the goodbye to an empty room and seal the Visit
 *    Record unseen. The moment is instead ARMED (SF_FAREWELL_ARM +
 *    depart_due_ts) and held. While it is pending the Visitor carries on
 *    completely normally - it is still fed, still sleeps, still ages.
 *
 *    VISIT_HOLD_MAX_HOURS is a PRIORITY escalation, not an override. Past it
 *    the departure outranks everything else that could delay it - a game, an
 *    open menu, a mischief window - and fires the instant it can. It does NOT
 *    outrank sleep. An earlier version put the cap above the sleep gate, so a
 *    48-hour-old pending departure could wake a dark room at 3 am with a
 *    goodbye nobody saw; that is the exact failure this whole rule exists to
 *    prevent. The order is now: asleep -> wait, always; awake and capped ->
 *    go now, unconditionally; awake and under the cap -> wait for the screen.
 *
 *    So the worst case is bounded by the WAKE CLOCK, not by the cap: a
 *    departure that comes due at 3 am on day one of a long absence waits
 *    until SLEEP_END_HOUR on the day the device is next switched on. The note
 *    acknowledges the wait once it has been pending for over an hour.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "visitrec.h"      /* FAREWELL_MAX */

#ifdef __cplusplus
extern "C" {
#endif

/* Compose the note from the current Visitor's history. Deterministic for a
 * given history. */
void farewell_compose(char *out, size_t len);

void farewell_begin(void);        /* enter the farewell state + screen */
bool farewell_active(void);

/* Has the visit reached its end AND can it be seen? Checked once per sim
 * tick. Arms the hold on the first call after the date passes. */
bool farewell_due(void);

/* Test hook: acknowledge without a tap. Same path as the button. */
void farewell_acknowledge(void);

/* --- the visit model ---------------------------------------------------- */

/* Advance the departure model by `hours` of SIMULATED time. Called from
 * care_advance(), so the live tick, the fast-forward hook and every offline
 * chunk all drive it through one path. */
void visit_advance(float hours);

/* 0..1 stay quality, derived from evolve_scores(). */
float visit_stay01(void);

/* Where the model would put departure RIGHT NOW, before drift clamping,
 * the notice floor and the lock. */
float visit_target_day(void);

/* The projected departure day actually in force (persisted, clamped, and
 * frozen once locked). 0 before the first evaluation. */
float visit_depart_day(void);

/* Hours until departure; negative once overdue. */
float visit_hours_left(void);
bool  visit_locked(void);

/* Day of the improvement-only adult form re-check: partway through the
 * adult stretch, which is a variable length now. */
float visit_recheck_day(void);

/* 1 Hz. Drives the foreshadowing bubbles, which are gated on the lock. */
void visit_tick(void);

/* Console report - cs, ba, stay01, target vs in-force date, lock state,
 * hours remaining, plus the calibration table for all four seeds. */
void visit_report(void);

/* Reset the departure projection for a brand-new Visitor. */
void visit_reset(void);

#ifdef __cplusplus
}
#endif
