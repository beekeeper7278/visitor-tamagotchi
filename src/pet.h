#pragma once
/* ===========================================================================
 * pet - the live PetState in RAM. Single source of truth for all stats.
 *
 * MILESTONE 3A: fixed demo values, NO passive decay and NO time simulation.
 * The Stats page needs something to render and the HUD needs a mood, but
 * introducing decay here would drag the whole time-driven simulation into a
 * milestone that is meant to be about navigation.
 *
 * MILESTONE 3B adds the food, bathroom, mess, weight and cleanliness
 * mechanics on top - and only those. Elapsed-time catch-up across reboots,
 * sleep and aging stay with their own later phases.
 *
 * NOT PERSISTED YET. Save/load integration is a later roadmap item; this
 * state lives in RAM and resets on boot. storage.* is untouched.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mood - pure function of state, first match wins [SPEC section 2] */
typedef enum {
    MOOD_ASLEEP = 0, MOOD_SLEEPY, MOOD_HUNGRY, MOOD_YUCKY,
    MOOD_GRUMPY, MOOD_EXCITED, MOOD_PLAYFUL, MOOD_CONTENT
} mood_t;

typedef struct {
    float    hunger, happiness, discipline, cleanliness, energy;   /* 0..100 */
    float    weight_g;
    uint8_t  stage;          /* 0 baby .. */
    uint8_t  form_id;
    uint16_t days_alive;       /* derived cache of pet_age_days()        */
    uint16_t days_alive_max;   /* monotonic floor                        */
    bool     asleep;

    /* --- 3B --- */
    float    bathroom;       /* 0..100, 100 = accident imminent            */
    uint8_t  mess_count;
    uint16_t meals, cakes_eaten, times_dirty, accidents, lights_forgotten;

    /* --- 6: persistence / catch-up anchors --- */
    uint32_t hatch_ts;       /* 0 until the clock is first trusted         */
    uint32_t last_sim_ts;    /* last moment the simulation was up to date  */
    /* The day each stage was ENTERED, indexed by stage. Recorded against the
     * configured boundary, not the day the transition was noticed, so an
     * offline gap that spans several stages still yields correct history. */
    uint16_t stage_day[5];

    /* --- 8: care-history accumulators [SPEC section 3] ------------------
     * Exponential moving averages sampled hourly, half-life
     * ACCUM_HALFLIFE_HOURS (12 h since the 1/3/6-day lifecycle; it was 24 h).
     * The half-life IS the recovery guarantee: two days of perfect care lifts
     * a bottomed-out 20 to 95, and one bad day drops a perfect 100 to 40 -
     * which twelve hours of good care clears back over 70. Evolution
     * therefore reads recent care, not ancient mistakes. */
    float    care_happy, care_fed, care_clean, care_sleep, care_discipline;
    float    nutrition;              /* 100 x junk/meals; 0 is best        */
    float    acc_hours;              /* hours of sample accumulated        */

    /* per-stage counters, reset at each transition */
    uint16_t ignored_requests, games_played, junk_meals;
    uint16_t disc_correct, disc_unfair;
    uint16_t stage_start_day;

    /* --- personality + favourites --- */
    uint8_t  trait_a, trait_b;       /* PERS_*                             */
    uint16_t food_count[3];          /* burger / fruit / cake              */
    uint8_t  mischief;               /* hidden tendency, 0..100            */
    uint8_t  evo_announce;           /* a form change waiting to be SHOWN  */
    uint8_t  evo_path[4];            /* form at baby / kid / teen / adult  */
    uint16_t disc_opportunities, disc_ignored;

    /* --- the egg --- */
    uint8_t  egg_color;      /* RESOLVED palette index; cosmetic only     */
    uint8_t  egg_choice;     /* what the player picked; 6 = Random        */
    float    bath_target_h;  /* awake hours for THIS cycle; persisted     */
    uint32_t egg_hatch_ts;   /* unix seconds when it hatches; 0 = not started */

    /* --- the variable visit [PACING PASS] -------------------------------
     * See farewell.h. depart_day is the PROJECTED departure in fractional
     * age days; it is persisted, drift-limited and eventually frozen, so it
     * is state rather than a derived value. */
    float    depart_day;     /* 0 = not projected yet                      */
    uint32_t depart_due_ts;  /* when it fell due; 0 = has not fallen due   */
    uint8_t  depart_locked;  /* frozen inside VISIT_DEPART_LOCK_HOURS      */
    uint8_t  stay_band;      /* 0 short 1 middle 2 long                    */
} pet_state_t;

void pet_init(void);
const pet_state_t *pet_get(void);

mood_t      pet_mood(void);
const char *pet_mood_name(mood_t m);
const char *pet_stage_name(uint8_t stage);

/* Normalised weight for the renderer's +/-20% body_w modifier. */
float pet_weight_norm(void);

/* Advance the stage to whatever `day` implies, stepping through EVERY
 * boundary crossed in order and logging each - so an offline gap that spans
 * Baby to Teen records both transitions rather than jumping silently. The
 * stage only; the FORM is Phase 8. Returns the number of transitions made. */
uint8_t pet_apply_stage_for_day(float day);

/* AGE IS DERIVED FROM THE CLOCK, never accumulated.
 *
 * It used to be advanced only in sim_catch_up() as
 *     days_alive += elapsed_sec / 86400
 * which had three separate faults: care_tick() never aged the Visitor at all,
 * so a device left switched on never changed stage; the integer division
 * returned 0 for any absence under 24 h, so twelve two-hour gaps advanced the
 * age by nothing; and hatch_ts was stored but never used to derive anything.
 *
 * Returns 0 when there is no hatch timestamp or the clock is untrusted. */
float pet_age_days(void);

/* Refresh the days_alive cache and days_alive_max from the clock. Cheap;
 * called from the 1 s tick. */
void  pet_refresh_age(void);

/* Mutable access for the care mechanics. Deliberately not part of the public
 * read-only surface that ui uses: ui reads pet, only care writes it. */
pet_state_t *pet_mutable(void);

/* Freeze all simulation and user-driven mutation. Exists so the persistence
 * fidelity test can prove pack/save/load is exact INDEPENDENTLY of the time
 * simulator - otherwise a decayed value is indistinguishable from a lost one. */
void pet_set_sim_suspended(bool on);
bool pet_sim_suspended(void);

#ifdef __cplusplus
}
#endif
