#pragma once
/* ===========================================================================
 * evolve - care-history accumulators, personality, favourites  [MILESTONE 8]
 *
 * Evolution reads ACCUMULATED HISTORY, not the meters on screen right now.
 * The accumulators are EMAs whose half-life is what makes recovery mandatory
 * and provable. It was 24 h when Baby lasted 3 days; the 1/3/6-day lifecycle
 * rescaled it to 12 h (ACCUM_HALFLIFE_HOURS), and both guarantees below were
 * recomputed for that value:
 *   two days of perfect care from a bottomed-out 20
 *       -> 100 - 80 * 2^(-48/12) = 95      (a top form is still reachable)
 *   one bad day from a perfect 100
 *       -> 20 + 80 * 2^(-24/12)  = 40      (harsh, but 12 h of care clears 70)
 *
 * Those two numbers are the entire recovery guarantee and both are testable.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Personality: two traits per Visitor, not a cloud of tiny random values.
 * Two is enough to feel individual and few enough to actually author lines
 * for. */
enum {
    PERS_PLAYFUL = 0, PERS_SLEEPY, PERS_DRAMATIC, PERS_TIDY,
    PERS_MISCHIEVOUS, PERS_FOODIE, PERS_COMPETITIVE, PERS_CURIOUS,
    PERS_SHY, PERS_COUNT
};

typedef struct {
    float cs;        /* care score        0..100  */
    float ba;        /* behaviour score -100..100 */
    float ia;        /* indulgence        0..100  */
    float engage;
    float ignored_per_day, unfair_per_day;
} evo_scores_t;

/* Sampled once per simulated hour from care_advance(). */
void evolve_accumulate(float hours, bool asleep);

evo_scores_t evolve_scores(void);

/* The per-stage rate denominator evolve_scores() divides by. Exposed so the
 * care-seeding test fixtures can invert it with the SAME expression rather
 * than a hand-copied one - they had already drifted apart, and a fixture that
 * silently produces half the engagement it claims corrupts every calibration
 * figure derived from it. */
float evolve_stage_days(void);

/* Choose the form for a stage the Visitor is ENTERING. Pure given the
 * accumulators; never re-derived for a stage already recorded. */
uint8_t evolve_pick_form(uint8_t stage);

/* Improvement-only re-check partway through the Adult stretch [SPEC section
 * 3]. The day is visit_recheck_day(), not a hardcoded 18: the visit is
 * variable now, so a fixed day is a fixed fraction of nothing. Returns the
 * new form or the current one; never regresses. */
uint8_t evolve_midadult_recheck(void);

void evolve_new_personality(void);
const char *evolve_trait_name(uint8_t t);
uint8_t evolve_favourite_food(void);     /* 0 burger 1 fruit 2 cake */
const char *evolve_food_name(uint8_t f);

/* Per-stage counters reset at each transition [SPEC section 3]. */
void evolve_on_stage_entered(uint8_t stage, uint16_t day);

/* "Why did this Visitor evolve into this form?" with the contributing
 * numbers - the selection must be explainable, not just correct. */
void evolve_explain(void);

/* Run the transformation for a form change, and record the milestone.
 * `announce_only` is the post-offline reveal: the form is already correct in
 * the save, we just have not SHOWN it yet. */
void evolve_present(uint8_t new_form, bool announce_only);

/* Called at boot. If a form change happened while powered off, show it ONCE
 * and clear the persisted flag - never replay it on later reboots. */
void evolve_check_announce(void);

#ifdef __cplusplus
}
#endif
