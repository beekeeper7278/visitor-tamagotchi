/* persist - live Visitor <-> save_t. See persist.h. */

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "storage.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "persist.h"

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

    b->meals            = p->meals;
    b->cakes_eaten      = p->cakes_eaten;
    b->times_dirty      = p->times_dirty;
    b->lights_forgotten = p->lights_forgotten;

    /* --- schema 2 --- */
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
    if (r == LOAD_OK || r == LOAD_MIGRATED) unpack(&b);
    Serial.printf("SAVE load: %s\n", storage_load_result_str(r));
    if (r == LOAD_MIGRATED)
        Serial.println("  schema 1 -> 2 migrated: bathroom and per-mess records added");
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
