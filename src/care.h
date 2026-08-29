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

/* Driven from t_anim (10 fps): food-drop, eat/refuse and the clean sequence.
 * The 1 s sim tick is far too coarse for presentation. */
void care_anim_tick(void);

feed_result_t care_feed(food_t f);
void          care_bathroom(void);      /* Care page button                 */
void          care_clean(void);         /* removes ALL messes               */

bool        care_is_holding(void);      /* urgent -> renderer holding pose  */
uint8_t     care_mess_count(void);
const char *care_food_name(food_t f);
const char *care_feed_result_name(feed_result_t r);

/* Test hook: advance the simulation by n minutes so hours-long behaviour can
 * be verified without waiting hours. */
void care_fast_forward(uint32_t minutes);
void care_report(void);

#ifdef __cplusplus
}
#endif
