/* pet - live PetState. See pet.h for what each milestone owns. */

#include <Arduino.h>
#include "config.h"
#include "forms.h"
#include <string.h>
#include "pet.h"
#include "evolve.h"
#include "journal.h"
#include "rtc.h"

static pet_state_t s;

void pet_init(void)
{
    /* MILESTONE 3A demo values. Chosen so the Stats page shows four visibly
     * DIFFERENT bar lengths - four identical bars would hide a layout bug
     * that only shows up with real data later. */
    s.hunger      = 72.0f;
    s.happiness   = 88.0f;
    s.discipline  = 45.0f;
    s.cleanliness = 61.0f;
    s.energy      = 80.0f;
    s.weight_g    = 45.0f;
    s.stage       = 0;
    s.form_id     = FORM_BABY;
    s.days_alive  = 0;   /* a newborn is day 0, not day 1 */
    s.days_alive_max = 0;
    s.asleep      = false;
    s.bathroom    = 0.0f;
    s.mess_count  = 0;
    s.meals = s.cakes_eaten = s.times_dirty = s.accidents = 0;
    s.lights_forgotten = 0;
    memset(s.stage_day, 0, sizeof(s.stage_day));
    s.care_happy = s.care_fed = s.care_clean = 60.0f;
    s.care_sleep = s.care_discipline = 50.0f;
    s.nutrition = 0.0f;
    s.acc_hours = 0.0f;
    s.ignored_requests = s.games_played = s.junk_meals = 0;
    s.disc_correct = s.disc_unfair = 0;
    s.stage_start_day = 0;
    s.trait_a = s.trait_b = 0;
    memset(s.food_count, 0, sizeof(s.food_count));
    s.mischief = 20;
    s.evo_announce = 0;
    memset(s.evo_path, 0, sizeof(s.evo_path));
    s.disc_opportunities = s.disc_ignored = 0;
    /* Arrive as an egg: stage EGG, no hatch timestamp yet. The colour is
     * rolled now so the player sees it immediately. */
    s.stage        = STAGE_EGG;
    s.egg_color    = (uint8_t)random(0, EGG_PALETTE_COUNT);
    s.egg_choice   = EGG_PALETTE_COUNT;    /* defaults to Random */
    s.bath_target_h = 0.0f;                /* drawn on the first cycle */
    s.egg_hatch_ts = 0;
    s.depart_day    = 0.0f;                /* projected on the first eval */
    s.depart_due_ts = 0;
    s.depart_locked = 0;
    s.stay_band     = 0;
    /* Identity defaults BEFORE anything is picked. Surprise for both, so a
     * player who presses START without touching a selector still gets a
     * resolved, persisted identity rather than a silent default. */
    s.gender          = GENDER_BOY;
    s.gender_choice   = GENDER_SURPRISE;
    s.learned_mischief = LEARN_START;
    memset(s.dream_id, 0, sizeof(s.dream_id));
    s.dream_n         = 0;
    s.sleep_accum_sec = 0;
    s.sleep_flags     = 0;
    s.pending_dream   = 0;
    evolve_new_personality();
    s.hatch_ts = 0;
    s.last_sim_ts = 0;
}

const pet_state_t *pet_get(void) { return &s; }

static bool s_suspended = false;
void pet_set_sim_suspended(bool on) { s_suspended = on; }
bool pet_sim_suspended(void)        { return s_suspended; }

/* First match wins, exactly as specified. Deliberately a pure function of
 * state with no globals and no LVGL, so it can be reasoned about on its own. */
mood_t pet_mood(void)
{
    if (s.asleep)                              return MOOD_ASLEEP;
    if (s.energy < 20.0f)                      return MOOD_SLEEPY;
    if (s.hunger < 25.0f)                      return MOOD_HUNGRY;
    if (s.mess_count > 0 || s.cleanliness < 30.0f) return MOOD_YUCKY;
    if (s.happiness < 30.0f)                   return MOOD_GRUMPY;
    if (s.happiness > 80.0f && s.energy > 50.0f) return MOOD_EXCITED;
    /* Playful (no game in 4 h) needs game history, which arrives with the
     * games phase. Until then it can never match, which is correct rather
     * than approximated. */
    return MOOD_CONTENT;
}

const char *pet_mood_name(mood_t m)
{
    switch (m) {
        case MOOD_ASLEEP:  return "Asleep";
        case MOOD_SLEEPY:  return "Sleepy";
        case MOOD_HUNGRY:  return "Hungry";
        case MOOD_YUCKY:   return "Yucky";
        case MOOD_GRUMPY:  return "Grumpy";
        case MOOD_EXCITED: return "Excited";
        case MOOD_PLAYFUL: return "Playful";
        default:           return "Content";
    }
}

const char *pet_stage_name(uint8_t stage)
{
    switch (stage) {
        case STAGE_EGG:  return "Egg";
        case STAGE_BABY: return "Baby";
        case STAGE_KID:  return "Kid";
        case STAGE_TEEN: return "Teen";
        default:         return "Adult";
    }
}

float pet_age_days(void)
{
    if (!s.hatch_ts || !rtc_trusted()) return 0.0f;
    const uint32_t now = rtc_now();
    if (!now || now <= s.hatch_ts) return 0.0f;
    return (float)(now - s.hatch_ts) / 86400.0f;
}

void pet_refresh_age(void)
{
    const float d = pet_age_days();
    const uint16_t whole = (uint16_t)d;
    s.days_alive = whole;
    /* Monotonic: a clock correction can never make the Visitor younger. */
    if (whole > s.days_alive_max) s.days_alive_max = whole;
    if (s.days_alive < s.days_alive_max) s.days_alive = s.days_alive_max;
}

static uint8_t stage_for_day(float day)
{
    if (day >= STAGE_DAY_ADULT) return STAGE_ADULT;
    if (day >= STAGE_DAY_TEEN)  return STAGE_TEEN;
    if (day >= STAGE_DAY_KID)   return STAGE_KID;
    return STAGE_BABY;
}

uint8_t pet_apply_stage_for_day(float day)
{
    /* Without a hatch timestamp there is no real calendar age, so the
     * Visitor stays an Egg rather than aging off an untrusted clock. */
    if (!s.hatch_ts) {
        if (s.stage != STAGE_EGG) s.stage = STAGE_EGG;
        return 0;
    }

    const uint8_t target = stage_for_day(day);
    uint8_t moved = 0;

    /* Step through every boundary in order, recording each against the day it
     * ACTUALLY belongs to rather than the day we noticed. An 8-day absence
     * must record Baby->Kid at day 3 and Kid->Teen at day 7, not both at
     * day 8 - the journal and evolution will read these timings later, and
     * "everything happened at once" would be wrong history. */
    while (s.stage < target) {
        const uint8_t from = s.stage;
        s.stage++;
        moved++;
        const float boundary =
            (s.stage == STAGE_KID)   ? STAGE_DAY_KID   :
            (s.stage == STAGE_TEEN)  ? STAGE_DAY_TEEN  :
            (s.stage == STAGE_ADULT) ? STAGE_DAY_ADULT : 0.0f;
        s.stage_day[s.stage] = (uint16_t)boundary;
        Serial.printf("STAGE: %s -> %s (on day %.2f; noticed on day %.2f)\n",
                      pet_stage_name(from), pet_stage_name(s.stage),
                      (double)boundary, (double)day);
    }
    return moved;
}

/* --- the evolution path -------------------------------------------------- */

void pet_record_form(void)
{
    if (s.stage < STAGE_BABY || s.stage > STAGE_ADULT) return;
    const uint8_t slot = (uint8_t)(s.stage - STAGE_BABY);   /* baby -> 0 */
    if (s.evo_path[slot] == s.form_id) return;              /* already right */
    const uint8_t was = s.evo_path[slot];
    s.evo_path[slot] = s.form_id;
    Serial.printf("EVO PATH: %s slot = %s%s\n", pet_stage_name(s.stage),
                  forms_name(s.form_id),
                  was ? " (revised by the mid-adult re-check)" : "");
}

uint8_t pet_backfill_evo_path(void)
{
    if (s.stage < STAGE_BABY) return 0;
    uint8_t filled = 0;

    /* SLOT 0 NEEDS NO BACKFILL AND NO SENTINEL. FORM_BABY is 0, so a zero in
     * evo_path[0] is indistinguishable from "never recorded" - and it does
     * not matter, because every Visitor was a Baby and there is exactly one
     * Baby form. The slot is therefore CORRECT either way, and readers treat
     * index 0 as Baby unconditionally rather than testing it for truth. That
     * ambiguity is only harmless at index 0: for Kid, Teen and Adult, form 0
     * is not a legal value, so zero genuinely does mean "not recorded". */
    s.evo_path[0] = FORM_BABY;

    /* The CURRENT stage's slot is knowable - it is the form on screen. */
    const uint8_t slot = (uint8_t)(s.stage - STAGE_BABY);
    if (slot > 0 && slot < 4 && !s.evo_path[slot]) {
        s.evo_path[slot] = s.form_id;
        filled++;
    }

    /* Intermediate stages the Visitor demonstrably lived through but whose
     * form was never recorded stay BLANK on purpose. Inventing one would
     * mean re-running evolve_pick_form() against today's accumulators and
     * writing the answer into the past, which is exactly what the migration
     * rules forbid. The Journal renders a blank as "(not recorded)" and says
     * so, which is honest; a fabricated Bright Teen would not be. */
    if (filled)
        Serial.printf("EVO PATH: backfilled %u slot(s) from an older save\n", filled);
    return filled;
}

float pet_weight_norm(void)
{
    const float lo = PET_WEIGHT_MIN_G, hi = PET_WEIGHT_MAX_G;
    float n = (s.weight_g - lo) / (hi - lo);
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return n;
}

/* Internal accessor for the 3B mechanics, which live in pet_care.cpp. */
pet_state_t *pet_mutable(void) { return &s; }
