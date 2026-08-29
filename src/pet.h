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
    uint16_t days_alive;
    bool     asleep;

    /* --- 3B --- */
    float    bathroom;       /* 0..100, 100 = accident imminent            */
    uint8_t  mess_count;
    uint16_t meals, cakes_eaten, times_dirty, accidents;
} pet_state_t;

void pet_init(void);
const pet_state_t *pet_get(void);

mood_t      pet_mood(void);
const char *pet_mood_name(mood_t m);
const char *pet_stage_name(uint8_t stage);

/* Normalised weight for the renderer's +/-20% body_w modifier. */
float pet_weight_norm(void);

/* Mutable access for the care mechanics. Deliberately not part of the public
 * read-only surface that ui uses: ui reads pet, only care writes it. */
pet_state_t *pet_mutable(void);

#ifdef __cplusplus
}
#endif
