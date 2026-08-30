/* evolve - accumulators, form selection, personality. See evolve.h. */

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "forms.h"
#include "pet.h"
#include "care.h"
#include "gamerec.h"
#include "evolve.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "persist.h"
#include "journal.h"

static float clamp01_100(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

/* EMA toward `sample` with a 24 h half-life [SPEC section 3]. */
static void ema(float *acc, float sample, float hours)
{
    const float alpha = 1.0f - powf(2.0f, -hours / ACCUM_HALFLIFE_HOURS);
    *acc += alpha * (sample - *acc);
}

/* care_fed rewards a FED pet, not a stuffed one: 100 in the comfortable
 * band, falling linearly to 0 at either starving or permanently full. */
static float fed_sample(float hunger)
{
    if (hunger >= 40.0f && hunger <= 90.0f) return 100.0f;
    if (hunger < 40.0f)  return clamp01_100(hunger / 40.0f * 100.0f);
    return clamp01_100((100.0f - hunger) / 10.0f * 100.0f);
}

void evolve_accumulate(float hours, bool asleep)
{
    if (hours <= 0.0f) return;
    pet_state_t *p = pet_mutable();

    ema(&p->care_happy,      p->happiness,   hours);
    ema(&p->care_fed,        fed_sample(p->hunger), hours);
    ema(&p->care_clean,      p->cleanliness, hours);
    ema(&p->care_discipline, p->discipline,  hours);

    /* Sleep quality is only meaningful while asleep; between nights the
     * accumulator holds its last value rather than decaying toward nothing. */
    if (asleep) {
        const float q = care_lights_on() ? 45.0f : 95.0f;
        ema(&p->care_sleep, q, hours);
    }

    const float junk = (p->meals > 0)
        ? 100.0f * (float)p->junk_meals / (float)p->meals : 0.0f;
    ema(&p->nutrition, junk, hours);

    p->acc_hours += hours;
}

evo_scores_t evolve_scores(void)
{
    const pet_state_t *p = pet_get();
    evo_scores_t s;

    /* Floored at 1.0. Over a 1-day Baby stage the old +0.5 smoothing let a
     * single ignored request read as 0.67/day where the 3-day stage scored
     * the same behaviour at 0.29/day - identical care, far worse score,
     * purely because the stage got shorter. The floor keeps per-day rates
     * comparable across stages of very different lengths. */
    float stage_days = pet_age_days() - (float)p->stage_start_day + 0.5f;
    if (stage_days < 1.0f) stage_days = 1.0f;

    s.engage = clamp01_100(100.0f * (float)p->games_played / (2.0f * stage_days));

    s.cs = 0.30f * p->care_happy + 0.22f * p->care_fed + 0.18f * p->care_clean
         + 0.15f * p->care_sleep + 0.15f * s.engage;

    s.ignored_per_day = (float)p->ignored_requests / stage_days;
    s.unfair_per_day  = (float)p->disc_unfair / stage_days;

    s.ba = 1.4f * (p->care_discipline - 50.0f)
         - 3.0f * s.ignored_per_day - 8.0f * s.unfair_per_day;
    if (s.ba < -100.0f) s.ba = -100.0f;
    if (s.ba >  100.0f) s.ba =  100.0f;

    /* Indulgence: junk DIET first, carried weight second. Rebalanced from
     * .6/.4 when cake's weight effect was slowed: if visible weight climbs
     * more gradually, a weight-dominant IA would make Chonky nearly
     * unreachable. Diet is the honest signal for "cake-powered" anyway. */
    s.ia = 0.7f * p->nutrition
         + 0.3f * clamp01_100((p->weight_g - 50.0f) / 0.5f);

    return s;
}

uint8_t evolve_pick_form(uint8_t stage)
{
    const pet_state_t *p = pet_get();
    evo_scores_t s = evolve_scores();

    /* Neutral zone: anything within EVO_EPS of the line takes the kinder
     * branch by documented tie-break, rather than following FP residue. */
    if (stage == STAGE_KID) {
        if (s.ba >= -EVO_EPS) return FORM_KID_GOOD;     /* includes the tie */
        if (s.cs >= 55.0f - EVO_EPS) return FORM_KID_GOOD;
        return FORM_KID_MISCHIEF;
    }

    if (stage == STAGE_TEEN) {
        /* +/-5 bias from the kid form. Deliberately small: one full stage of
         * good care overrides it completely, so a Good Kid with bad teen care
         * does NOT get an automatic pass. */
        float cs = s.cs + (p->form_id == FORM_KID_GOOD ? 5.0f : -5.0f);
        if (cs >= 65.0f - EVO_EPS && s.ba >= -EVO_EPS) return FORM_TEEN_BRIGHT;
        /* Rowdy needs to be clearly past the line, not merely touching it. */
        if (cs < 45.0f - EVO_EPS || s.ba < -25.0f - EVO_EPS) return FORM_TEEN_ROWDY;
        return FORM_TEEN_MELLOW;
    }

    if (stage == STAGE_ADULT) {
        /* Ordered, first match wins. No teen bias here - the adult form is
         * decided purely on teen-stage care. */
        if (s.ia >= 65.0f - EVO_EPS && s.cs >= 35.0f - EVO_EPS) return FORM_ADULT_CHONKY;
        /* The two negative forms must be clearly earned, so their thresholds
         * lean the generous way too. */
        if (p->care_clean < 40.0f - EVO_EPS)                return FORM_ADULT_SCRUFFY;
        if (p->care_happy < 40.0f - EVO_EPS ||
            p->care_sleep < 40.0f - EVO_EPS)                return FORM_ADULT_GRUMPY;
        if (s.cs >= 78.0f - EVO_EPS && s.ba >= 10.0f - EVO_EPS &&
            p->care_clean >= 60.0f - EVO_EPS)               return FORM_ADULT_BEST;
        if (s.engage >= 70.0f - EVO_EPS)                    return FORM_ADULT_PLAYFUL;
        return FORM_ADULT_SWEET;
    }
    return FORM_BABY;
}

/* Best > Sweet > Playful > Chonky > Grumpy > Scruffy [SPEC section 3] */
static int adult_rank(uint8_t f)
{
    switch (f) {
        case FORM_ADULT_BEST:    return 6;
        case FORM_ADULT_SWEET:   return 5;
        case FORM_ADULT_PLAYFUL: return 4;
        case FORM_ADULT_CHONKY:  return 3;
        case FORM_ADULT_GRUMPY:  return 2;
        case FORM_ADULT_SCRUFFY: return 1;
        default:                 return 0;
    }
}

uint8_t evolve_midadult_recheck(void)
{
    const pet_state_t *p = pet_get();
    const uint8_t want = evolve_pick_form(STAGE_ADULT);
    /* Improvement only. Without this, days 13-21 have no stakes and a
     * redeemed Visitor gets no acknowledgement - but it must never regress,
     * or late neglect would feel like a punishment rather than a story. */
    return (adult_rank(want) > adult_rank(p->form_id)) ? want : p->form_id;
}

void evolve_on_stage_entered(uint8_t stage, uint16_t day)
{
    pet_state_t *p = pet_mutable();

    /* GROWTH SPURT. Pull excess weight partway toward the new stage's
     * baseline, so an older form looks newly grown rather than carrying the
     * exact same chubbiness forward. Only the VISIBLE weight moves: meals,
     * cakes, junk_meals and the nutrition accumulator are all untouched, so
     * an overeating history can still produce a Chonky Adult later. */
    const float base = (stage == STAGE_KID)   ? STAGE_BASELINE_KID_G
                     : (stage == STAGE_TEEN)  ? STAGE_BASELINE_TEEN_G
                     : (stage == STAGE_ADULT) ? STAGE_BASELINE_ADULT_G
                                              : STAGE_BASELINE_BABY_G;
    if (p->weight_g > base) {
        const float before = p->weight_g;
        p->weight_g -= (p->weight_g - base) * GROWTH_SPURT_FRACTION;
        Serial.printf("GROWTH SPURT: %.1f g -> %.1f g (baseline %.0f, history kept)\n",
                      before, p->weight_g, base);
    }
    p->ignored_requests = 0;
    p->games_played     = 0;
    p->junk_meals       = 0;
    p->meals            = 0;
    p->disc_correct     = 0;
    p->disc_unfair      = 0;
    p->stage_start_day  = day;
    (void)stage;
}

/* --- personality --------------------------------------------------------- */

static const char *TRAIT[PERS_COUNT] = {
    "playful", "sleepy", "dramatic", "tidy", "mischievous",
    "food-loving", "competitive", "curious", "shy"
};

const char *evolve_trait_name(uint8_t t)
{
    return (t < PERS_COUNT) ? TRAIT[t] : "?";
}

void evolve_new_personality(void)
{
    pet_state_t *p = pet_mutable();
    p->trait_a = (uint8_t)random(0, PERS_COUNT);
    do { p->trait_b = (uint8_t)random(0, PERS_COUNT); } while (p->trait_b == p->trait_a);
    /* Mischievous Visitors start with a higher hidden tendency, which is the
     * only way personality touches the care rules at all. */
    p->mischief = (p->trait_a == PERS_MISCHIEVOUS || p->trait_b == PERS_MISCHIEVOUS)
                ? 45 : 20;
    Serial.printf("PERSONALITY: %s and %s (mischief %u)\n",
                  TRAIT[p->trait_a], TRAIT[p->trait_b], p->mischief);
}

/* --- favourites, from what actually happened ----------------------------- */

const char *evolve_food_name(uint8_t f)
{
    return f == 0 ? "burger" : f == 1 ? "fruit" : "cake";
}

uint8_t evolve_favourite_food(void)
{
    const pet_state_t *p = pet_get();
    uint8_t best = 0; uint16_t n = 0;
    for (uint8_t i = 0; i < 3; i++)
        if (p->food_count[i] > n) { n = p->food_count[i]; best = i; }
    /* Nothing eaten yet: personality breaks the tie rather than a coin flip,
     * so even an early answer says something true about this Visitor. */
    if (n == 0)
        return (p->trait_a == PERS_FOODIE || p->trait_b == PERS_FOODIE) ? 2 : 0;
    return best;
}

/* --- the explanation ----------------------------------------------------- */

static const char *evo_line(void)
{
    const pet_state_t *p = pet_get();
    const uint8_t a = p->trait_a, b = p->trait_b;
    if (a == PERS_DRAMATIC    || b == PERS_DRAMATIC)    return "BEHOLD! I changed!";
    if (a == PERS_SHY         || b == PERS_SHY)         return "Um... do I look different?";
    if (a == PERS_MISCHIEVOUS || b == PERS_MISCHIEVOUS) return "New look. Same trouble.";
    if (a == PERS_COMPETITIVE || b == PERS_COMPETITIVE) return "Look at me now!";
    return "Whoa! I changed!";
}

static void evo_done_cb(pet_anim_t finished)
{
    if (finished != PET_ANIM_EVOLVING) return;
    ui_bubble_set_suppressed(false);
    ui_pet_set_wander(true);
    ui_bubble_say(BUBBLE_T1_REACTION, evo_line());
}

void evolve_present(uint8_t new_form, bool announce_only)
{
    pet_state_t *p = pet_mutable();

    /* Interrupt whatever was happening, and stop unrelated chatter talking
     * over the moment. Both are restored by the completion callback. */
    ui_bubble_set_suppressed(true);
    ui_pet_set_wander(false);

    if (!announce_only) p->form_id = new_form;
    p->evo_announce = 0;

    ui_pet_set_done_cb(evo_done_cb);
    ui_pet_evolve_to(new_form);

    journal_add(JM_EVOLVED, new_form, 0);
    Serial.printf("EVOLVE PRESENT: -> %s%s\n", forms_name(new_form),
                  announce_only ? " (offline reveal)" : "");
    persist_mark_dirty("evolved");
}

void evolve_check_announce(void)
{
    pet_state_t *p = pet_mutable();
    if (!p->evo_announce) return;
    /* One reveal, however many boundaries the absence crossed: the Visitor
     * ends in the correct final form and gets a single transformation, not a
     * queue of them back to back. */
    Serial.println("EVOLVE: a form change happened while you were away");
    evolve_present(p->form_id, true);
}

void evolve_explain(void)
{
    const pet_state_t *p = pet_get();
    evo_scores_t s = evolve_scores();

    Serial.println();
    Serial.println("=== WHY THIS FORM? ========================================");
    Serial.printf("  current    : %s (stage %s, day %u)\n",
                  forms_name(p->form_id), pet_stage_name(p->stage), p->days_alive);
    Serial.printf("  personality: %s + %s   mischief %u\n",
                  evolve_trait_name(p->trait_a), evolve_trait_name(p->trait_b),
                  p->mischief);
    Serial.println("  accumulators (EMA, 24h half-life):");
    Serial.printf("    happy %.1f  fed %.1f  clean %.1f  sleep %.1f  disc %.1f  junk %.1f\n",
                  p->care_happy, p->care_fed, p->care_clean, p->care_sleep,
                  p->care_discipline, p->nutrition);
    Serial.println("  composites:");
    /* Three decimals: a decision taken inside the neutral zone must be
     * explainable, and "-0.0" explains nothing. */
    Serial.printf("    CS %8.3f = .30*%.2f + .22*%.2f + .18*%.2f + .15*%.2f + .15*%.2f(engage)\n",
                  s.cs, p->care_happy, p->care_fed, p->care_clean, p->care_sleep, s.engage);
    Serial.printf("    BA %8.3f = 1.4*(%.3f-50) - 3*%.3f(ignored/d) - 8*%.3f(unfair/d)\n",
                  s.ba, p->care_discipline, s.ignored_per_day, s.unfair_per_day);
    Serial.printf("    IA %8.3f = .7*%.2f(junk) + .3*weight(%.1fg)\n",
                  s.ia, p->nutrition, p->weight_g);
    Serial.printf("    neutral zone +/-%.2f: BA is %s, CS-55 is %s\n", EVO_EPS,
                  fabsf(s.ba) <= EVO_EPS ? "INSIDE (tie-break applies)" : "outside",
                  fabsf(s.cs - 55.0f) <= EVO_EPS ? "INSIDE" : "outside");
    Serial.printf("  stage counters: games %u  meals %u (junk %u)  ignored %u  "
                  "disc ok %u / unfair %u\n",
                  p->games_played, p->meals, p->junk_meals, p->ignored_requests,
                  p->disc_correct, p->disc_unfair);
    Serial.printf("  would pick now: Kid->%s  Teen->%s  Adult->%s\n",
                  forms_name(evolve_pick_form(STAGE_KID)),
                  forms_name(evolve_pick_form(STAGE_TEEN)),
                  forms_name(evolve_pick_form(STAGE_ADULT)));
    Serial.printf("  favourite food: %s   favourite game: %s\n",
                  evolve_food_name(evolve_favourite_food()),
                  gamerec_name(gamerec_favorite()));
    Serial.printf("  RECOVERY (half-life %.0f h): 2 days perfect from 20 -> 95;\n",
                  (double)ACCUM_HALFLIFE_HOURS);
    Serial.println("            one bad day from 100 -> 40, back over 70 in 12 h.");
    Serial.println("-----------------------------------------------------------");
}
