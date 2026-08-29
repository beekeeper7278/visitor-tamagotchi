/* pet - live PetState. See pet.h for what each milestone owns. */

#include <Arduino.h>
#include "config.h"
#include "forms.h"
#include "pet.h"

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
    s.days_alive  = 1;
    s.asleep      = false;
    s.bathroom    = 0.0f;
    s.mess_count  = 0;
    s.meals = s.cakes_eaten = s.times_dirty = s.accidents = 0;
}

const pet_state_t *pet_get(void) { return &s; }

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
        case 0:  return "Baby";
        case 1:  return "Kid";
        case 2:  return "Teen";
        default: return "Adult";
    }
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
