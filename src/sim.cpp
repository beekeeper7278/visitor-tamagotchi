/* sim - elapsed-time reconstruction. See sim.h for the rules. */

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "sim.h"
#include "gamerec.h"
#include "evolve.h"
#include "forms.h"
#include "farewell.h"

static sim_report_t s_rep;

bool sim_is_sleep_hour(uint8_t hour, uint8_t stage, bool *is_nap)
{
    if (is_nap) *is_nap = false;

    /* Night window wraps midnight, so the test is an OR, not a range. */
    if (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR) return true;

    if (stage <= NAP_MAX_STAGE && hour >= NAP_START_HOUR && hour < NAP_END_HOUR) {
        if (is_nap) *is_nap = true;
        return true;
    }
    return false;
}

void sim_catch_up(uint32_t from_ts, uint32_t to_ts, sim_report_t *out)
{
    memset(&s_rep, 0, sizeof(s_rep));

    if (!from_ts || to_ts <= from_ts) {
        if (out) *out = s_rep;
        return;
    }

    pet_state_t *p = pet_mutable();

    s_rep.ran           = true;
    s_rep.elapsed_sec   = to_ts - from_ts;
    s_rep.hunger_before = p->hunger;
    s_rep.happy_before  = p->happiness;
    s_rep.clean_before  = p->cleanliness;
    s_rep.energy_before = p->energy;
    s_rep.bath_before   = p->bathroom;
    s_rep.days_before   = p->days_alive;

    /* One damage budget for the WHOLE absence, spent across every chunk.
     * Time itself is never budgeted - only harm. */
    sim_budget_t b;
    b.hunger_left    = OFFLINE_HUNGER_MAX_DROP;
    b.happy_left     = OFFLINE_HAPPY_MAX_DROP;
    b.clean_left     = OFFLINE_CLEAN_MAX_DROP;
    b.accidents_left = OFFLINE_MAX_ACCIDENTS;
    b.cap_hunger = b.cap_happy = b.cap_clean = b.cap_accident = false;

    const uint8_t accidents_before = (uint8_t)p->accidents;

    /* Walk the whole interval in chunks. Chunking is what makes sleep and
     * time-of-day boundaries land correctly: a single multi-day dt would
     * apply one wrong rate across the entire gap. */
    for (uint32_t t = from_ts; t < to_ts; t += SIM_CHUNK_SEC) {
        uint32_t step = SIM_CHUNK_SEC;
        if (t + step > to_ts) step = to_ts - t;

        const uint8_t hour = (uint8_t)((t % 86400UL) / 3600UL);
        bool nap = false;
        const bool asleep = sim_is_sleep_hour(hour, p->stage, &nap);

        /* Lights are whatever they were left as - that is the point of the
         * forgotten-lights mechanic. */
        const bool lights = care_lights_on();

        sim_ctx_t ctx = { asleep, lights, true, asleep, nap };
        care_advance(step * 1000UL, &ctx, &b);

        if (asleep) {
            s_rep.sleep_chunks++;
            if (nap) s_rep.nap_chunks++;
            if (lights) s_rep.lights_on_sleep_chunks++;
        } else {
            s_rep.awake_chunks++;
        }
        s_rep.chunks++;
    }

    /* Age advances by the FULL elapsed interval - never capped. */
    /* Age comes from the clock, not from adding up absences: the old
     * accumulation truncated every gap under 24 h to zero. */
    pet_refresh_age();
    p->asleep = false;      /* you are here now, so it is awake */

    /* Every stage boundary the absence crossed, ONE AT A TIME.
     *
     * This used to be `if (pet_apply_stage_for_day(...) > 0)`, which walked
     * every boundary inside the condition and then did the per-boundary work
     * exactly ONCE, for the final stage. An absence spanning Baby -> Kid ->
     * Teen therefore never picked a Kid form at all: evo_path[] was left
     * blank at Kid, and - worse - the teen selector's "was a Good Kid" +/-5
     * bias read a form_id that was still FORM_BABY, so a Visitor cared for
     * well while the device was off was structurally denied the bonus it had
     * earned. Measured offline against the same care history crossing the
     * same boundaries with the device on: the teen form differed in 39% of
     * cases and evo_path[] in 100%. */
    bool crossed_boundary = false;
    while (pet_apply_one_stage(pet_age_days())) {
        crossed_boundary = true;
        gamerec_on_stage_change(p->stage);   /* fresh bests for a new tier */
        /* Pick the form for the stage just ENTERED, from the history as it
         * stood. Never recomputed on a later boot: once chosen it is the
         * Visitor's actual past. */
        const uint8_t f = evolve_pick_form_on(p->stage,
                                             (float)p->stage_day[p->stage]);
        if (f != p->form_id) {
            Serial.printf("EVOLVE: %s -> %s (stage %s)\n",
                          forms_name(p->form_id), forms_name(f),
                          pet_stage_name(p->stage));
            p->form_id = f;
            p->evo_announce = 1;    /* shown once on return, not now */
            s_rep.evolved = true;
        }
        /* Record the form against the stage it belongs to. This used to be
         * an inline index expression guarded by `p->stage < 4`, which
         * silently excluded STAGE_ADULT (4) - so the final form of every
         * Visitor was missing from its own history. pet_record_form() knows
         * the slot mapping in one place. */
        pet_record_form();
        /* The day the stage was ENTERED, not the day the absence ended.
         * p->days_alive is the end-of-absence day, so a Kid stage entered on
         * day 1 during a jump to day 4 was given stage_start_day 4 - which
         * makes evolve_stage_days() (and therefore `engage`, and therefore
         * CS) wrong for the stage that was just decided. stage_day[] is
         * written by pet_apply_one_stage() with the real boundary. */
        evolve_on_stage_entered(p->stage, p->stage_day[p->stage]);
    }
    /* The mid-adult glow-up is the ELSE of the boundary walk: a tick that
     * just crossed into Adult has already picked its form and must not
     * immediately re-check it. `crossed_boundary` rather than
     * s_rep.evolved, because a boundary can be crossed WITHOUT the form
     * changing and that still counts as having just been decided. */
    if (!crossed_boundary && p->stage == STAGE_ADULT &&
        pet_age_days() >= visit_recheck_day()) {
        /* Was a hardcoded "day 18" - a fixed fraction of the old fixed 21-day
         * visit, and meaningless against a variable one. Now halfway through
         * whatever adult stretch this Visitor actually has. */
        const uint8_t f = evolve_midadult_recheck();
        if (f != p->form_id) {
            Serial.printf("EVOLVE: glow-up %s -> %s\n",
                          forms_name(p->form_id), forms_name(f));
            p->form_id = f;
            p->evo_announce = 1;
            s_rep.evolved = true;
            pet_record_form();      /* the glow-up REVISES the adult slot */
        }
    }

    /* One "you left the lights on" mark per absence, not one per chunk -
     * otherwise a single forgetful night would score hundreds. */
    if (s_rep.lights_on_sleep_chunks > 0) p->lights_forgotten++;

    s_rep.hunger_after = p->hunger;
    s_rep.happy_after  = p->happiness;
    s_rep.clean_after  = p->cleanliness;
    s_rep.energy_after = p->energy;
    s_rep.bath_after   = p->bathroom;
    s_rep.days_after   = p->days_alive;
    s_rep.accident     = ((uint8_t)p->accidents != accidents_before);
    s_rep.messes_kept  = care_mess_count();
    s_rep.budget       = b;

    if (out) *out = s_rep;
}

const sim_report_t *sim_last_report(void) { return &s_rep; }

/* ONE greeting, highest priority first - never a stack. Tone stays funny and
 * kid-friendly: a child who left the device in a drawer for a week should
 * still want to pick it up again, so none of these scold. */
const char *sim_return_greeting(const sim_report_t *r)
{
    if (!r || !r->ran || r->elapsed_sec < (uint32_t)OFFLINE_MIN_NOTICE_SEC) return nullptr;

    if (r->accident)                       return "Um... I had a little accident.";
    if (r->hunger_after < 25.0f)           return "I'm STARVING!";
    if (r->messes_kept > 0)                return "It got a bit messy in here!";
    if (r->elapsed_sec >= (uint32_t)OFFLINE_LONG_ABSENCE_SEC)
                                           return "WHERE HAVE YOU BEEN?!";
    if (r->sleep_chunks > r->awake_chunks) return "*yawn* ...oh, hi!";
    return "You're back!";
}

void sim_print_report(void)
{
    const sim_report_t *r = &s_rep;
    Serial.println();
    Serial.println("=== OFFLINE CATCH-UP ======================================");
    if (!r->ran) {
        Serial.println("  no trusted elapsed time - nothing reconstructed");
        Serial.println("-----------------------------------------------------------");
        return;
    }

    const uint32_t e = r->elapsed_sec;
    Serial.printf("  elapsed        : %lu s  (%lud %luh %lum)  in %lu chunks\n",
                  (unsigned long)e, (unsigned long)(e / 86400),
                  (unsigned long)((e % 86400) / 3600),
                  (unsigned long)((e % 3600) / 60), (unsigned long)r->chunks);
    Serial.printf("  age            : day %u -> %u\n", r->days_before, r->days_after);
    Serial.printf("  hunger         : %.0f -> %.0f\n", r->hunger_before, r->hunger_after);
    Serial.printf("  happiness      : %.0f -> %.0f\n", r->happy_before,  r->happy_after);
    Serial.printf("  cleanliness    : %.0f -> %.0f\n", r->clean_before,  r->clean_after);
    Serial.printf("  energy         : %.0f -> %.0f\n", r->energy_before, r->energy_after);
    Serial.printf("  bathroom       : %.0f -> %.0f\n", r->bath_before,   r->bath_after);
    Serial.printf("  accident       : %s\n", r->accident ? "YES - one, and only one" : "no");
    Serial.printf("  messes on floor: %u (persisted, not respawned)\n", r->messes_kept);
    Serial.printf("  sleep periods  : %lu asleep (%lu nap), %lu awake, %lu with lights ON\n",
                  (unsigned long)r->sleep_chunks, (unsigned long)r->nap_chunks,
                  (unsigned long)r->awake_chunks,
                  (unsigned long)r->lights_on_sleep_chunks);

    Serial.print("  caps reached   : ");
    if (!r->budget.cap_hunger && !r->budget.cap_happy &&
        !r->budget.cap_clean && !r->budget.cap_accident) {
        Serial.println("none");
    } else {
        if (r->budget.cap_hunger)   Serial.print("hunger ");
        if (r->budget.cap_happy)    Serial.print("happiness ");
        if (r->budget.cap_clean)    Serial.print("cleanliness ");
        if (r->budget.cap_accident) Serial.print("accident-count ");
        Serial.println();
    }
    Serial.println("  (time, age and stage timing are never capped)");
    Serial.println("-----------------------------------------------------------");
}
