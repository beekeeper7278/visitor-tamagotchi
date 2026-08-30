/* persist - live Visitor <-> save_t. See persist.h. */

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "storage.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "persist.h"
#include "evolve.h"
#include "journal.h"
#include "ui_pet.h"

static bool     s_dirty;
static uint32_t s_last_try_ms;

static void pack(save_t *b)
{
    const pet_state_t *p = pet_get();
    storage_defaults(b);

    b->hatch_ts     = p->hatch_ts;
    b->last_sim_ts  = p->last_sim_ts;
    b->last_rtc_ts  = rtc_trusted() ? rtc_now() : 0;

    b->hunger       = p->hunger;
    b->happiness    = p->happiness;
    b->discipline   = p->discipline;
    b->cleanliness  = p->cleanliness;
    b->energy       = p->energy;
    b->weight_g     = p->weight_g;

    b->stage        = p->stage;
    b->form_id      = p->form_id;
    b->days_alive_max = p->days_alive;

    b->flags = 0;
    if (p->asleep)        b->flags |= SF_ASLEEP;
    if (care_lights_on()) b->flags |= SF_LIGHTS_ON;
    if (!rtc_trusted())   b->flags |= SF_RTC_SUSPECT;
    if (p->hatch_ts)      b->flags |= SF_HATCHED;
    /* Armed = the departure moment has passed but nobody has SEEN the
     * goodbye yet. depart_due_ts carries when; this flag carries that it
     * is outstanding, which is what SF_FAREWELL_ARM was reserved for. */
    if (p->depart_due_ts) b->flags |= SF_FAREWELL_ARM;

    b->meals            = p->meals;
    b->cakes_eaten      = p->cakes_eaten;
    b->times_dirty      = p->times_dirty;
    b->lights_forgotten = p->lights_forgotten;

    /* --- schema 2 --- */
    b->care_happy      = p->care_happy;
    b->care_fed        = p->care_fed;
    b->care_clean      = p->care_clean;
    b->care_sleep      = p->care_sleep;
    b->care_discipline = p->care_discipline;
    b->nutrition       = p->nutrition;
    b->ignored_requests = p->ignored_requests;
    b->games_played     = p->games_played;
    b->junk_meals       = p->junk_meals;
    b->disc_correct     = p->disc_correct;
    b->disc_unfair      = p->disc_unfair;
    b->personality_trait_1 = p->trait_a;
    b->personality_trait_2 = p->trait_b;
    b->mischief_tendency   = p->mischief;
    for (uint8_t i = 0; i < 3; i++) b->food_history[i] = p->food_count[i];
    b->stage_start_day     = p->stage_start_day;
    for (uint8_t i = 0; i < 5; i++) b->stage_day_entered[i] = p->stage_day[i];
    b->acc_hours           = p->acc_hours;
    b->evo_announce        = p->evo_announce;
    for (uint8_t i = 0; i < 4; i++) b->evo_path[i] = p->evo_path[i];
    b->disc_opportunities  = p->disc_opportunities;
    b->disc_ignored        = p->disc_ignored;
    b->egg_color           = p->egg_color;
    b->egg_choice          = p->egg_choice;
    b->egg_hatch_ts        = p->egg_hatch_ts;
    b->bath_target_h       = p->bath_target_h;
    b->depart_day          = p->depart_day;
    b->depart_due_ts       = p->depart_due_ts;
    b->depart_locked       = p->depart_locked;
    b->stay_band           = p->stay_band;
    b->gender              = p->gender;
    b->gender_choice       = p->gender_choice;
    b->learned_mischief    = p->learned_mischief;
    for (uint8_t i = 0; i < DREAM_KEEP; i++) b->dream_id[i] = p->dream_id[i];
    b->dream_n             = p->dream_n;
    b->sleep_accum_sec     = p->sleep_accum_sec;
    b->sleep_flags         = p->sleep_flags;
    b->pending_dream       = p->pending_dream;
    journal_store(b);

    b->bathroom  = (uint8_t)(p->bathroom + 0.5f);
    b->accidents = p->accidents;

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t type, food; bool bitten; uint32_t age_ms; float drained;
        int16_t x, y;
        if (!care_mess_snapshot(i, &type, &food, &bitten, &age_ms, &drained, &x, &y)) {
            b->mess_type[i] = 0;
            continue;
        }
        b->mess_type[i]    = type;
        b->mess_food[i]    = (uint8_t)(food | (bitten ? MESS_BITTEN_BIT : 0));
        uint32_t mins = age_ms / 60000UL;
        b->mess_age_min[i] = (uint16_t)(mins > 65535UL ? 65535UL : mins);
        b->mess_drained[i] = (uint8_t)(drained > 255.0f ? 255 : (uint8_t)(drained + 0.5f));
        b->mess_x2[i]      = (uint8_t)(x < 0 ? 0 : (x / 2 > 255 ? 255 : x / 2));
        b->mess_y2[i]      = (uint8_t)(y < 0 ? 0 : (y / 2 > 255 ? 255 : y / 2));
    }
}

static void unpack(const save_t *b)
{
    pet_state_t *p = pet_mutable();

    p->hatch_ts    = b->hatch_ts;
    p->last_sim_ts = b->last_sim_ts;

    p->hunger      = b->hunger;
    p->happiness   = b->happiness;
    p->discipline  = b->discipline;
    p->cleanliness = b->cleanliness;
    p->energy      = b->energy;
    p->weight_g    = b->weight_g;

    p->stage       = b->stage;
    p->form_id     = b->form_id;
    p->days_alive  = b->days_alive_max;

    p->asleep      = (b->flags & SF_ASLEEP) != 0;
    care_set_lights((b->flags & SF_LIGHTS_ON) != 0);

    p->meals            = b->meals;
    p->cakes_eaten      = b->cakes_eaten;
    p->times_dirty      = b->times_dirty;
    p->lights_forgotten = b->lights_forgotten;

    p->care_happy      = b->care_happy;
    p->care_fed        = b->care_fed;
    p->care_clean      = b->care_clean;
    p->care_sleep      = b->care_sleep;
    p->care_discipline = b->care_discipline;
    p->nutrition       = b->nutrition;
    p->ignored_requests = b->ignored_requests;
    p->games_played     = b->games_played;
    p->junk_meals       = b->junk_meals;
    p->disc_correct     = b->disc_correct;
    p->disc_unfair      = b->disc_unfair;
    p->trait_a  = b->personality_trait_1;
    p->trait_b  = b->personality_trait_2;
    p->mischief = b->mischief_tendency;
    for (uint8_t i = 0; i < 3; i++) p->food_count[i] = b->food_history[i];
    p->stage_start_day = b->stage_start_day;
    for (uint8_t i = 0; i < 5; i++) p->stage_day[i] = b->stage_day_entered[i];
    p->acc_hours    = b->acc_hours;
    p->evo_announce = b->evo_announce;
    for (uint8_t i = 0; i < 4; i++) p->evo_path[i] = b->evo_path[i];
    p->disc_opportunities = b->disc_opportunities;
    p->disc_ignored       = b->disc_ignored;
    p->egg_color          = b->egg_color;
    p->egg_choice         = b->egg_choice;
    p->egg_hatch_ts       = b->egg_hatch_ts;
    p->bath_target_h      = b->bath_target_h;
    p->depart_day         = b->depart_day;
    p->depart_due_ts      = b->depart_due_ts;
    p->depart_locked      = b->depart_locked;
    p->stay_band          = b->stay_band;
    p->gender             = b->gender;
    p->gender_choice      = b->gender_choice;
    p->learned_mischief   = b->learned_mischief;
    for (uint8_t i = 0; i < DREAM_KEEP; i++) p->dream_id[i] = b->dream_id[i];
    p->dream_n            = b->dream_n;
    if (p->dream_n > DREAM_KEEP) p->dream_n = DREAM_KEEP;
    p->sleep_accum_sec    = b->sleep_accum_sec;
    p->sleep_flags        = b->sleep_flags;
    p->pending_dream      = b->pending_dream;
    journal_load(b);

    p->bathroom  = (float)b->bathroom;
    p->accidents = b->accidents;

    /* Messes are restored as the SAME messes: type, food, bitten state,
     * position and the drain they have already charged. Restoring the drain
     * is what stops a reboot either double-charging cleanliness or resetting
     * a mess's accumulated penalty back to zero. */
    for (uint8_t i = 0; i < 4; i++) {
        if (b->mess_type[i] == 0) continue;
        care_restore_mess(b->mess_type[i],
                          (uint8_t)(b->mess_food[i] & ~MESS_BITTEN_BIT),
                          (b->mess_food[i] & MESS_BITTEN_BIT) != 0,
                          (uint32_t)b->mess_age_min[i] * 60000UL,
                          (float)b->mess_drained[i],
                          (int16_t)(b->mess_x2[i] * 2),
                          (int16_t)(b->mess_y2[i] * 2));
    }
    p->mess_count = care_mess_count();
}

load_result_t persist_load(void)
{
    save_t b;
    const load_result_t r = storage_load(&b);
    if (r == LOAD_OK || r == LOAD_MIGRATED) {
        unpack(&b);
        /* Validate the personality rather than assuming it. A save migrated
         * up from v1/v2 has zeroed fields, which read as trait 0 twice and a
         * mischief of 0 - "playful and playful", incapable of mischief.
         *
         * Reroll ONLY when the stored values are genuinely invalid. A valid
         * personality must survive every reboot untouched, or the Visitor
         * quietly becomes a different character each time it is switched on. */
        pet_state_t *p = pet_mutable();

        /* A dev save may already be past the NEW 1/3/6 boundaries. Recompute
         * the stage from age, but NEVER re-announce: evo_announce is cleared
         * and evo_path is left alone, because the forms it already holds are
         * the Visitor's actual past. Re-running evolve_pick_form() here would
         * rewrite history from today's accumulators. */
        if (r == LOAD_MIGRATED && p->hatch_ts) {
            const uint8_t was = p->stage;
            const uint8_t moved = pet_apply_stage_for_day(pet_age_days());
            if (moved) {
                p->evo_announce = 0;      /* migration is not an event */
                Serial.printf("MIGRATION: stage recomputed %s -> %s under the "
                              "new boundaries (no replay)\n",
                              pet_stage_name(was), pet_stage_name(p->stage));
            }
        }

        const bool bad_range  = (p->trait_a >= PERS_COUNT) || (p->trait_b >= PERS_COUNT);
        const bool bad_same   = (p->trait_a == p->trait_b);
        const bool bad_misch  = (p->mischief > 100);
        if (bad_range || bad_same || bad_misch) {
            Serial.printf("SAVE: personality invalid (%s%s%s) - rolling a new one\n",
                          bad_range ? "trait out of range " : "",
                          bad_same  ? "traits identical "   : "",
                          bad_misch ? "mischief out of range" : "");
            evolve_new_personality();
            ui_pet_set_baby_palette(p->egg_color);
        } else {
            ui_pet_set_baby_palette(p->egg_color);
            Serial.printf("SAVE: personality kept - %s + %s (mischief %u)\n",
                          evolve_trait_name(p->trait_a),
                          evolve_trait_name(p->trait_b), p->mischief);
        }

        /* Learned behaviour, validated the same way and for the same reason:
         * a field that arrives as a NaN or an out-of-range float from a
         * corrupt-but-CRC-valid tail would otherwise poison every mischief
         * roll for the rest of the visit. */
        if (!(p->learned_mischief >= 0.0f) || p->learned_mischief > 100.0f) {
            Serial.println("SAVE: learned behaviour out of range - back to neutral");
            p->learned_mischief = LEARN_START;
        }
        /* Identity is presentation only, so an invalid value is repaired
         * silently rather than rerolled loudly - but it IS repaired, because
         * the Journal prints it. */
        if (p->gender > GENDER_GIRL) p->gender = GENDER_BOY;
        if (p->gender_choice > GENDER_SURPRISE) p->gender_choice = GENDER_SURPRISE;

        /* THE "HOW I GREW UP" BACKFILL. Saves written before Phase 9.5 have a
         * mostly-empty evo_path[] - the old writer only ran in the offline
         * path and could never record an Adult. Fill in what is KNOWABLE
         * (Baby, and the form on screen now); anything else stays blank and
         * the Journal says so rather than inventing history. */
        pet_backfill_evo_path();
    }
    Serial.printf("SAVE load: %s\n", storage_load_result_str(r));
    if (r == LOAD_MIGRATED)
        /* Which hop it was is not knowable here - storage.cpp has already
         * rewritten the header to the current schema. Naming a specific pair
         * was wrong from the moment a third schema existed, and it printed
         * "schema 1 -> 2" for a v5 save. */
        Serial.printf("  migrated forward to schema %u: new fields zeroed, "
                      "nothing replayed\n", SAVE_SCHEMA_VERSION);
    return r;
}

bool persist_save(bool force)
{
    save_t b;
    pack(&b);
    const bool ok = storage_save(&b, force);
    if (ok) s_dirty = false;
    return ok;
}

void persist_mark_dirty(const char *reason)
{
    s_dirty = true;
    /* Try immediately: storage_save() enforces the minimum interval and the
     * shadow compare, so an "important transition" save is a request, never
     * a bypass of the flash-wear design. */
    if (persist_save(false))
        Serial.printf("SAVE (%s)\n", reason ? reason : "state change");
}

void persist_tick(void)
{
    if (!s_dirty) return;
    const uint32_t now = millis();
    if (now - s_last_try_ms < 5000UL) return;     /* don't hammer the floor */
    s_last_try_ms = now;
    persist_save(false);
}

void persist_print_state(const char *tag)
{
    const pet_state_t *p = pet_get();
    uint8_t type=0, food=0; bool bitten=false; uint32_t age=0; float dr=0;
    int16_t x=-1, y=-1;
    const bool has = care_mess_snapshot(0, &type, &food, &bitten, &age, &dr, &x, &y);

    Serial.printf("[%s] hun %.2f  hap %.2f  dis %.2f  cln %.2f  eng %.2f\n",
                  tag, p->hunger, p->happiness, p->discipline,
                  p->cleanliness, p->energy);
    Serial.printf("[%s] weight %.2f  bathroom %.2f  lights %s  stage %u  day %u\n",
                  tag, p->weight_g, p->bathroom, care_lights_on() ? "ON" : "OFF",
                  p->stage, p->days_alive);
    if (has)
        Serial.printf("[%s] mess0 type %u food %u bitten %d at (%d,%d) age %lus drained %.1f\n",
                      tag, type, food, (int)bitten, (int)x, (int)y,
                      (unsigned long)(age / 1000), dr);
    else
        Serial.printf("[%s] mess0 none\n", tag);
}

void persist_report(void)
{
    Serial.printf("SAVE: writes %lu, shadow-skipped %lu, dirty %s\n",
                  (unsigned long)storage_write_count(),
                  (unsigned long)storage_skipped_count(),
                  s_dirty ? "yes" : "no");
}
