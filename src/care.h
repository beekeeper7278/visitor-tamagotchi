#pragma once
/* ===========================================================================
 * care - food, bathroom, messes, cleanliness, weight  [MILESTONE 3B]
 *
 * Owns the mechanics the Food and Care pages drive, plus the room mess pool.
 * Every rate and timer lives in config.h, never here.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>
#include "sim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FOOD_BURGER = 0, FOOD_FRUIT, FOOD_CAKE } food_t;

typedef enum {
    FEED_EATEN = 0,     /* eaten in full                                    */
    FEED_PARTIAL,       /* very full: half a burger, remainder dropped      */
    FEED_REFUSED,       /* completely full: declined                        */
    FEED_DROPPED        /* declined AND dropped on the floor                */
} feed_result_t;

void care_init(lv_obj_t *room_layer);

/* Driven from t_sim (1 s). Time-driven, not tick-driven: it computes its own
 * dt, so a starved timer loses no simulated time. */
void care_tick(void);

/* THE shared stat-advance step. Used by the 1 s live tick and by every
 * offline catch-up chunk - one implementation, so a bug shows up in normal
 * play rather than only after a three-day absence. */
void care_advance(uint32_t dt_ms, const sim_ctx_t *ctx, sim_budget_t *b);

/* Restore a mess exactly as it was, for load-from-save. Type, food, bitten
 * state, position and accumulated drain are all preserved - persisted messes
 * are never respawned as generic new ones. */
void care_restore_mess(uint8_t type, uint8_t food, bool bitten,
                       uint32_t age_ms, float drained,
                       int16_t x, int16_t y);
uint8_t care_mess_snapshot(uint8_t idx, uint8_t *type, uint8_t *food,
                           bool *bitten, uint32_t *age_ms, float *drained,
                           int16_t *x, int16_t *y);

/* Driven from t_anim (10 fps): food-drop, eat/refuse and the clean sequence.
 * The 1 s sim tick is far too coarse for presentation. */
void care_anim_tick(void);

feed_result_t care_feed(food_t f);
void          care_bathroom(void);      /* Care page button                 */
void          care_clean(void);         /* removes ALL messes               */

bool        care_lights_on(void);
void        care_set_lights(bool on);

/* THE PLAYER pressing the switch, as opposed to the loader, the morning
 * restore or care_reset() setting it. Only this entry point produces a
 * reaction, and the reaction is DEFERRED - the switch lives on the Care
 * page, so the Visitor's answer has to survive until the menu closes.
 * Lights off never puts the Visitor to sleep; bedtime remains the clock's. */
void        care_player_toggle_lights(bool on);

/* TEST ONLY: FORCE a dream, bypassing every eligibility rule.
 *
 * The real path is the sleep period in care_advance() - accumulated
 * duration, one per period, persisted so reboots cannot duplicate. This
 * exists so the dream TABLE and the bubble can be exercised on demand
 * without waiting for a night, and it deliberately proves nothing about the
 * rules. care_dream_rules_probe() is what tests those. */
bool        care_dream(bool nap, bool defer);

/* TEST ONLY: run the real eligibility logic against constructed sleep
 * periods and report what each one decided - short night, long night, short
 * nap, long nap (as a distribution), and a second close of a period that has
 * already dreamt. Snapshots and restores the Visitor's own sleep state, so
 * running it does not change the pet. */
void        care_dream_rules_probe(void);

/* TEST ONLY: report what the old-mess comment logic can actually see, then
 * clear its cooldown and run the REAL trigger once.
 *
 * Sampling dialogue_stink() proves the line table and nothing else. What
 * needs proving is the GATE - that a mess has genuinely aged into its stink
 * lines, that the Visitor is awake and not mid-sequence, and that the
 * automatic path therefore fires. With a five-minute cooldown and a 45% roll
 * that is not observable by watching, so it gets a probe. */
void        care_stink_probe(void);

/* True when the clock says the Visitor should be asleep right now. Any
 * scripted action that finishes must consult this and put it back to bed. */
void        care_new_bath_target(void);
bool        care_sleep_due(void);

/* The same question, plus whether the window is an afternoon NAP. The sleep
 * period needs to know which kind it is at the moment it OPENS - deciding
 * later, from the clock, would misread a nap that ran into the evening. */
bool        care_sleep_due_nap(bool *is_nap);
void        care_return_to_bed(void);
bool        care_is_holding(void);      /* urgent -> renderer holding pose  */
uint8_t     care_mess_count(void);
const char *care_food_name(food_t f);
const char *care_feed_result_name(feed_result_t r);

/* Test hook: advance the simulation by n minutes so hours-long behaviour can
 * be verified without waiting hours. */
/* Back to a newborn room: clears every mess, cancels any sequence in flight
 * and restores autonomous wandering. */
void care_reset(void);

void care_fast_forward(uint32_t minutes);
void care_report(void);

#ifdef __cplusplus
}
#endif
