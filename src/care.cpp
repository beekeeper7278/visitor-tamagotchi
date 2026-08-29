/* ===========================================================================
 * care - food, bathroom, messes, cleanliness, weight  [MILESTONE 3B]
 *
 * Every rate, threshold, grace period and cap is a config.h constant. If you
 * find yourself wanting to change a number, it is up there, not in here.
 *
 * TIME-DRIVEN, NOT TICK-DRIVEN. advance() takes a dt in milliseconds and
 * applies rate_per_hour * dt / 3600000. A starved timer therefore loses no
 * simulated time, and the same function serves the fast-forward test hook -
 * one code path, so a bug in it shows up in normal play rather than only
 * after a long absence.
 * ======================================================================== */

#include <Arduino.h>
#include <math.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "pet.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "strings.h"
#include "care.h"
#include "menu.h"
#include "persist.h"
#include "rtc.h"
#include "sim.h"
#include "bsp.h"

typedef enum { MESS_NONE = 0, MESS_FOOD, MESS_POOP } mess_type_t;

typedef struct {
    mess_type_t type;
    food_t      food;        /* only meaningful for MESS_FOOD              */
    bool        bitten;      /* a half-eaten burger looks bitten           */
    bool        stinking;    /* stink lines added once it has sat a while  */
    lv_coord_t  x, y;        /* where it is drawn, so puffs land on it     */
    uint32_t    age_ms;      /* how long it has sat on the floor           */
    float       drained;     /* cleanliness this mess has already taken    */
    lv_obj_t   *obj;
} mess_t;

static mess_t    s_mess[MESS_MAX];
static lv_obj_t *s_room;
static uint32_t  s_last_ms;
static bool      s_bath_active;          /* mid run-off sequence           */
static uint32_t  s_last_warn_ms;
static uint32_t  s_urgent_since_ms;
static bool      s_was_urgent;
static uint32_t  s_sim_ms;               /* total simulated ms, for logs   */

const char *care_food_name(food_t f)
{
    switch (f) { case FOOD_BURGER: return "burger";
                 case FOOD_FRUIT:  return "fruit";
                 default:          return "cake"; }
}

const char *care_feed_result_name(feed_result_t r)
{
    switch (r) { case FEED_EATEN:   return "eaten";
                 case FEED_PARTIAL: return "half-eaten, rest dropped";
                 case FEED_REFUSED: return "refused";
                 default:           return "refused and dropped"; }
}

/* --- shape helpers -------------------------------------------------------
 * Everything here is built from plain rounded rectangles and circles. The
 * PNG seam is still empty by decision, and stacking three primitives makes a
 * far more readable burger than a single orange dot did. */

static lv_obj_t *shape(lv_obj_t *par, lv_coord_t w, lv_coord_t h,
                       lv_coord_t x, lv_coord_t y, lv_coord_t r, uint32_t col)
{
    lv_obj_t *o = lv_obj_create(par);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Food, drawn into a 40x34 container. Used BOTH for the item falling to the
 * Visitor and for what is left on the floor, so a dropped burger on the
 * carpet is recognisably the same burger you offered. */
static void build_food_shape(lv_obj_t *c, food_t f, bool bitten)
{
    lv_obj_clean(c);
    lv_obj_set_size(c, 40, 34);
    switch (f) {
    case FOOD_BURGER:
        shape(c,  34,  9,  3,  4, 5, 0xE0A863);   /* top bun            */
        shape(c,  38,  5,  1, 12, 2, 0x7BC46A);   /* lettuce            */
        shape(c,  36,  7,  2, 16, 3, 0x8A5433);   /* patty              */
        shape(c,  34,  8,  3, 22, 4, 0xD9A05C);   /* bottom bun         */
        shape(c,   5,  3, 10,  7, 2, 0xF2D6A8);   /* sesame highlight   */
        if (bitten) {
            /* Two background-coloured circles bite a chunk out of the right
             * edge. The screen is true black, so punching holes with black
             * circles is both correct and free. */
            shape(c, 18, 18, 26,  2, 9, 0x000000);
            shape(c, 13, 13, 30, 16, 7, 0x000000);
        }
        break;
    case FOOD_FRUIT:
        shape(c,  26, 26,  7,  7, 13, 0xE0524A);  /* apple body         */
        shape(c,   7,  8, 12,  4,  3, 0x7BC46A);  /* leaf               */
        shape(c,   3,  7, 19,  2,  1, 0x6B4A2F);  /* stem               */
        shape(c,   5,  8, 12, 13,  3, 0xF08A80);  /* shine              */
        break;
    default: /* FOOD_CAKE */
        shape(c,  32, 12,  4, 18, 3, 0xF6D9B0);   /* sponge             */
        shape(c,  32, 10,  4,  9, 4, 0xFF9EB5);   /* frosting           */
        shape(c,   8,  8, 16,  2, 4, 0xE0524A);   /* cherry             */
        break;
    }
}

/* A stacked coil rather than a brown blob - three tapering ellipses with a
 * little tip, which is what actually reads as poop at this size. */
static void build_poop_shape(lv_obj_t *c, bool stinking)
{
    lv_obj_clean(c);
    lv_obj_set_size(c, 34, 34);
    /* A layered, curled pile: three tapering tiers plus a little tip, with a
     * highlight on each so it reads as rounded rather than as flat blobs.
     * Cute-stylised on purpose - this is a kid's device. */
    shape(c, 32, 11,  1, 23, 6, 0x5A3A22);        /* base tier          */
    shape(c, 24, 10,  5, 16, 5, 0x6B4A2F);        /* middle tier        */
    shape(c, 15,  9, 10,  9, 5, 0x7B5636);        /* top tier           */
    shape(c,  6,  6, 14,  5, 3, 0x8A6340);        /* curl tip           */
    shape(c,  9,  3,  6, 25, 2, 0x6E4830);        /* base highlight     */
    shape(c,  6,  3,  9, 18, 2, 0x82593A);        /* middle highlight   */
    if (stinking) {
        /* faint wavy lines, only once it has been sitting a while */
        shape(c, 3, 8,  7, -4, 2, 0x3D5A44);
        shape(c, 3, 11, 15, -8, 2, 0x3D5A44);
        shape(c, 3, 8,  23, -4, 2, 0x3D5A44);
    }
}

/* --- mess sprites --------------------------------------------------------
 * Primitives, not PNGs. The asset seam is still empty by decision, and a
 * poop is a brown blob and a crumb is a small dot - both well within what
 * rounded rectangles express. Section 9 caps the pool at 4. */

static void mess_style(mess_t *m, uint8_t slot)
{
    if (!m->obj) return;
    /* deterministic spread from the slot index, so messes never stack on
     * exactly the same pixel and the count stays readable at a glance */
    const lv_coord_t x = 34 + slot * 76;
    const lv_coord_t y = ROOM_FLOOR_Y + (slot % 2) * 12;

    if (m->type == MESS_POOP) build_poop_shape(m->obj, m->stinking);
    else                      build_food_shape(m->obj, m->food, m->bitten);

    m->x = x; m->y = y;
    lv_obj_set_pos(m->obj, x, y);
    lv_obj_clear_flag(m->obj, LV_OBJ_FLAG_HIDDEN);
}

/* at_x < 0 means "put it in the next free floor slot" (accidents). An
 * explicit position keeps a half-eaten burger exactly where it was left. */
static bool mess_add_at(mess_type_t t, food_t f, bool bitten,
                        lv_coord_t at_x, lv_coord_t at_y)
{
    for (uint8_t i = 0; i < MESS_MAX; i++) {
        if (s_mess[i].type != MESS_NONE) continue;
        s_mess[i].type    = t;
        s_mess[i].food     = f;
        s_mess[i].bitten   = bitten;
        s_mess[i].stinking = false;
        s_mess[i].age_ms   = 0;
        s_mess[i].drained = 0.0f;
        if (!s_mess[i].obj && s_room) {
            s_mess[i].obj = lv_obj_create(s_room);
            lv_obj_remove_style_all(s_mess[i].obj);
            lv_obj_clear_flag(s_mess[i].obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
        mess_style(&s_mess[i], i);
        if (at_x >= 0) {                 /* override the slot position */
            s_mess[i].x = at_x;
            s_mess[i].y = at_y;
            lv_obj_set_pos(s_mess[i].obj, at_x, at_y);
        }
        pet_mutable()->mess_count = care_mess_count();
        pet_mutable()->times_dirty++;
        Serial.printf("MESS + %s  (now %u)\n",
                      t == MESS_POOP ? "accident" : "dropped food", care_mess_count());
        return true;
    }
    /* Pool full. Deliberately silent-but-logged rather than growing: four is
     * the sprite budget, and an unbounded pile would be both a memory leak
     * and an unwinnable cleanliness spiral. */
    Serial.println("MESS + ignored: floor already at MESS_MAX");
    return false;
}

static bool mess_add(mess_type_t t, food_t f, bool bitten)
{
    return mess_add_at(t, f, bitten, -1, -1);
}

uint8_t care_mess_count(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < MESS_MAX; i++) if (s_mess[i].type != MESS_NONE) n++;
    return n;
}

void care_init(lv_obj_t *room_layer)
{
    s_room = room_layer;
    for (uint8_t i = 0; i < MESS_MAX; i++) {
        s_mess[i].type = MESS_NONE;
        s_mess[i].obj  = nullptr;
    }
    s_last_ms = millis();
    ui_pet_set_done_cb([](pet_anim_t a) {
        if (a != PET_ANIM_BATHROOM) return;
        s_bath_active = false;
        ui_bubble_say(BUBBLE_T1_REACTION, "Oof... much better.");
    });
}

/* --- the simulation step ------------------------------------------------- */

static float clamp100(float v) { return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v); }

/* THE shared advance step - see care.h. Live ticks and offline catch-up
 * chunks both come through here. */
void care_advance(uint32_t dt_ms, const sim_ctx_t *ctx, sim_budget_t *b)
{
    if (dt_ms == 0) return;
    pet_state_t *p = pet_mutable();
    const float hours = (float)dt_ms / 3600000.0f;
    s_sim_ms += dt_ms;

    /* --- hunger ------------------------------------------------------- */
    float d_hun = (ctx->asleep ? RATE_HUNGER_ASLEEP : RATE_HUNGER_AWAKE) * hours;
    if (ctx->offline && b) {
        /* budget is a POSITIVE remaining-damage allowance */
        float want = -d_hun;
        if (want > b->hunger_left) { want = b->hunger_left; b->cap_hunger = true; }
        b->hunger_left -= want;
        d_hun = -want;
    }
    p->hunger = clamp100(p->hunger + d_hun);

    /* --- happiness: gentle, and never below the floor ------------------ */
    float d_hap = (ctx->asleep ? RATE_HAPPY_ASLEEP : RATE_HAPPY_AWAKE) * hours;
    if (ctx->offline && b) {
        float want = -d_hap;
        if (want > b->happy_left) { want = b->happy_left; b->cap_happy = true; }
        b->happy_left -= want;
        d_hap = -want;
    }
    float happy = p->happiness + d_hap;
    if (happy < HAPPINESS_DECAY_FLOOR) happy = HAPPINESS_DECAY_FLOOR;
    p->happiness = clamp100(happy);

    /* --- energy: sleep recovers, and lights-on recovers WORSE ---------- */
    float d_eng;
    if (ctx->asleep) {
        d_eng = (ctx->lights_on ? RATE_ENERGY_SLEEP_LIT : RATE_ENERGY_SLEEP_DARK) * hours;
    } else {
        d_eng = RATE_ENERGY_AWAKE * hours;
    }
    p->energy = clamp100(p->energy + d_eng);

    /* --- weight -------------------------------------------------------
     * Offline weight drift is deliberately OMITTED. Absence alone must not
     * change weight; it follows food and history rules only. */
    if (!ctx->offline) {
        p->weight_g += (ctx->asleep ? RATE_WEIGHT_ASLEEP : RATE_WEIGHT_AWAKE) * hours;
        if (p->weight_g < PET_WEIGHT_MIN_G) p->weight_g = PET_WEIGHT_MIN_G;
        if (p->weight_g > PET_WEIGHT_MAX_G) p->weight_g = PET_WEIGHT_MAX_G;
    }

    /* --- cleanliness: base drift + per-mess drain ---------------------- */
    float clean_take = -((ctx->asleep ? RATE_CLEAN_ASLEEP : RATE_CLEAN_AWAKE) * hours);

    for (uint8_t i = 0; i < MESS_MAX; i++) {
        if (s_mess[i].type == MESS_NONE) continue;
        s_mess[i].age_ms += dt_ms;

        if (!s_mess[i].stinking && s_mess[i].type == MESS_POOP &&
            s_mess[i].age_ms >= STINK_AFTER_MS) {
            s_mess[i].stinking = true;
            mess_style(&s_mess[i], i);
        }
        if (s_mess[i].age_ms < MESS_GRACE_MS) continue;

        const bool poop = (s_mess[i].type == MESS_POOP);
        const float rate = poop ? MESS_POOP_DRAIN_PER_H : MESS_FOOD_DRAIN_PER_H;
        const float cap  = poop ? MESS_POOP_DRAIN_CAP   : MESS_FOOD_DRAIN_CAP;

        float take = rate * hours;
        if (s_mess[i].drained + take > cap) take = cap - s_mess[i].drained;
        if (take <= 0.0f) continue;
        s_mess[i].drained += take;
        clean_take += take;
    }

    if (ctx->offline && b) {
        if (clean_take > b->clean_left) { clean_take = b->clean_left; b->cap_clean = true; }
        b->clean_left -= clean_take;
    }
    p->cleanliness = clamp100(p->cleanliness - clean_take);

    /* --- bathroom ------------------------------------------------------ */
    if (!s_bath_active) {
        p->bathroom = clamp100(p->bathroom + RATE_BATHROOM_PER_HOUR * hours);
    }

    /* Offline accidents: AT MOST ONE per absence. Without this the pet
     * would produce poop after poop in an empty room, which is both
     * unwinnable and not interesting. After the one accident the need is
     * satisfied and simply starts climbing again. */
    if (ctx->offline && b && p->bathroom >= 100.0f) {
        if (b->accidents_left > 0) {
            b->accidents_left--;
            p->bathroom = 0.0f;
            p->accidents++;
            /* Charge the accident to the SAME cleanliness budget as the mess
             * drain. Applying it outside the cap let a single absence take
             * more than OFFLINE_CLEAN_MAX_DROP while still reporting "no
             * caps reached" - the report and the behaviour disagreed. */
            float hit = BATHROOM_ACCIDENT_CLEAN;
            if (hit > b->clean_left) { hit = b->clean_left; b->cap_clean = true; }
            b->clean_left -= hit;
            p->cleanliness = clamp100(p->cleanliness - hit);
            mess_add(MESS_POOP, FOOD_BURGER, false);
        } else {
            b->cap_accident = true;
            /* Desperate, but with the live grace window intact - see
             * OFFLINE_BATHROOM_PARK_PCT for why this is not 100. */
            p->bathroom = OFFLINE_BATHROOM_PARK_PCT;
        }
    }
}

/* Bed and lights live further down with care_reset(); declared here because
 * care_tick() drives the bedtime sequence. */
static void build_bed(uint8_t stage);
static void bed_hide(void);
static lv_obj_t *s_bed;
static uint32_t  s_last_bright_complaint;
static bool      s_lights_on = true;   /* daytime default: lights on */

void care_tick(void)
{
    if (pet_sim_suspended()) { s_last_ms = millis(); return; }
    const uint32_t now = millis();
    const uint32_t dt = now - s_last_ms;
    s_last_ms = now;

    /* Live: no damage budget, and the sleep model supplies the context. */
    sim_ctx_t ctx = { pet_get()->asleep, care_lights_on(), false };
    care_advance(dt, &ctx, nullptr);

    pet_state_t *p = pet_mutable();

    /* Self-heal: if something interrupted the bathroom run, the completion
     * callback never fires, and a stuck s_bath_active would freeze the
     * bathroom need forever. Trust the renderer's actual state over our own
     * latched flag. */
    if (s_bath_active && ui_pet_current() != PET_ANIM_BATHROOM) {
        s_bath_active = false;
        Serial.println("BATHROOM sequence was interrupted - state recovered");
    }
    if (s_bath_active) return;

    const bool urgent = (p->bathroom >= BATHROOM_URGENT_PCT);

    if (urgent && !s_was_urgent) {
        s_urgent_since_ms = now;
        s_last_warn_ms = 0;
    }
    s_was_urgent = urgent;

    /* --- sleep presentation ------------------------------------------
     * Bedtime is a visible event: walk to the middle, a bed appears, the
     * Visitor settles into it. Waking restores daytime automatically. */
    if (rtc_trusted()) {
        rtc_time_t rt;
        if (rtc_read(&rt)) {
            bool nap = false;
            const bool should_sleep = sim_is_sleep_hour(rt.hour, p->stage, &nap);

            if (should_sleep && !p->asleep && !s_bath_active) {
                p->asleep = true;
                ui_pet_set_wander(false);
                build_bed(p->stage);
                ui_pet_walk_to(SLEEP_SPOT_X, SLEEP_SPOT_Y);
                Serial.printf("SLEEP: bedtime (%s), heading to bed\n",
                              nap ? "nap" : "night");
                persist_mark_dirty("bedtime");
            } else if (!should_sleep && p->asleep) {
                p->asleep = false;
                /* Morning restores daytime automatically - lights back ON
                 * and full brightness, whatever the player left them at. */
                care_set_lights(true);
                bed_hide();
                ui_pet_set_wander(true);
                ui_pet_play(PET_ANIM_HAPPY);        /* a little stretch */
                ui_bubble_say(BUBBLE_T2_MOOD, "Morning!");
                Serial.println("SLEEP: wake up - bed away, daytime restored");
                persist_mark_dirty("woke up");
            }

            if (p->asleep) {
                /* settle into the bed once the walk there finishes */
                if (ui_pet_current() == PET_ANIM_IDLE)
                    ui_pet_play(PET_ANIM_SLEEPING);

                /* Sleeping with the light on works, it is just worse - and
                 * the Visitor will occasionally say so, without nagging. */
                if (s_lights_on && now - s_last_bright_complaint >= LIGHTS_TOO_BRIGHT_MS) {
                    s_last_bright_complaint = now;
                    ui_bubble_say(BUBBLE_T2_MOOD,
                                  (now / 1000) % 2 ? "Turn the light off!"
                                                   : "It's too bright!");
                }
                return;     /* asleep: no holding pose, no wandering */
            }
        }
    }

    /* Feed urgency to the renderer so the squeeze and wiggle tighten as it
     * gets worse, instead of one flat "holding" look the whole time. */
    ui_pet_set_urgency((p->bathroom - BATHROOM_URGENT_PCT) /
                       (100.0f - BATHROOM_URGENT_PCT));

    if (urgent) {
        /* holding pose, unless something one-shot is playing */
        const pet_anim_t a = ui_pet_current();
        if (a == PET_ANIM_IDLE || a == PET_ANIM_WALK) ui_pet_play(PET_ANIM_HOLDING);

        if (now - s_last_warn_ms >= BATHROOM_WARN_MS) {
            s_last_warn_ms = now;
            ui_bubble_say(BUBBLE_T0_CRITICAL, strings_random(BUBBLE_T0_CRITICAL));
        }

        /* accident only after the grace period ON TOP of urgency, so there is
         * always a visible warning window first */
        if (now - s_urgent_since_ms >= BATHROOM_GRACE_MS || p->bathroom >= 100.0f) {
            p->bathroom = 0.0f;
            p->accidents++;
            p->cleanliness = clamp100(p->cleanliness - BATHROOM_ACCIDENT_CLEAN);
            mess_add(MESS_POOP, FOOD_BURGER, false);
            s_was_urgent = false;
            if (ui_pet_current() == PET_ANIM_HOLDING) ui_pet_play(PET_ANIM_SAD);
            ui_bubble_say(BUBBLE_T0_CRITICAL, "Uh oh... I couldn't hold it.");
            Serial.println("BATHROOM accident - mess left on the floor");
            persist_mark_dirty("accident");
        }
    } else if (ui_pet_current() == PET_ANIM_HOLDING) {
        ui_pet_play(PET_ANIM_IDLE);
    }
}

/* --- actions ------------------------------------------------------------- */

/* --- food presentation ---------------------------------------------------
 * The item falls to the Visitor, then the outcome PLAYS: eaten with a
 * chewing mouth, half-eaten then a head shake, or refused with a head shake
 * and the food left on the floor. Stat effects are applied when the sequence
 * resolves rather than on the button press, so what you see and what the
 * numbers do stay in step. */

typedef enum { FD_NONE = 0, FD_DROP, FD_APPROACH, FD_ACT } fd_phase_t;

static fd_phase_t   s_fd = FD_NONE;
static uint32_t     s_fd_t0;
static food_t       s_fd_food;
static feed_result_t s_fd_res;
static lv_obj_t    *s_fd_obj;
static uint32_t     s_fd_act_ms;
static lv_coord_t   s_fd_x, s_fd_y;      /* where the food comes to rest */
static bool         s_fd_bitten_shown;

/* Pure: works out what WOULD happen, without changing anything. */
static feed_result_t decide(food_t f, const pet_state_t *p)
{
    if (f == FOOD_CAKE)                     return FEED_EATEN;   /* always */
    if (p->hunger >= FOOD_FULL_PCT)         return FEED_DROPPED;
    if (f == FOOD_BURGER && p->hunger >= FOOD_PARTIAL_PCT) return FEED_PARTIAL;
    return FEED_EATEN;
}

static void apply_feed(food_t f, feed_result_t r)
{
    pet_state_t *p = pet_mutable();

    switch (r) {
    case FEED_EATEN:
        if (f == FOOD_CAKE) {
            p->hunger    = clamp100(p->hunger + CAKE_HUNGER);
            p->happiness = clamp100(p->happiness + CAKE_HAPPY);
            p->weight_g += CAKE_WEIGHT_G;
            p->cakes_eaten++;
        } else {
            p->hunger   = clamp100(p->hunger + (f == FOOD_BURGER ? BURGER_HUNGER : FRUIT_HUNGER));
            p->weight_g += (f == FOOD_BURGER ? BURGER_WEIGHT_G : FRUIT_WEIGHT_G);
        }
        p->meals++;
        ui_pet_play(PET_ANIM_EATING);
        ui_bubble_say(BUBBLE_T1_REACTION,
                      f == FOOD_CAKE ? "Cake! Yesss." : (f == FOOD_BURGER ? "Yum!" : "Crunchy!"));
        break;

    case FEED_PARTIAL:
        p->hunger    = clamp100(p->hunger + BURGER_HUNGER * FOOD_PARTIAL_FRACTION);
        p->weight_g += BURGER_WEIGHT_G * FOOD_PARTIAL_FRACTION;
        p->meals++;
        /* the leftover is placed by the sequence, at the food's location */
        ui_pet_play(PET_ANIM_EATING);
        ui_bubble_say(BUBBLE_T1_REACTION, "I can't finish it all...");
        break;

    default:    /* refused, and it ends up on the floor */
        mess_add_at(MESS_FOOD, f, false, s_fd_x, s_fd_y);   /* left where it fell */
        ui_pet_play(PET_ANIM_REFUSE);
        ui_bubble_say(BUBBLE_T1_REACTION, "No thanks, I'm stuffed!");
        break;
    }

    if (p->weight_g > PET_WEIGHT_MAX_G) p->weight_g = PET_WEIGHT_MAX_G;
    p->mess_count = care_mess_count();

    Serial.printf("FEED %-6s -> %-24s hunger %.0f  weight %.1f g\n",
                  care_food_name(f), care_feed_result_name(r), p->hunger, p->weight_g);
    persist_mark_dirty("fed");
}

feed_result_t care_feed(food_t f)
{
    if (s_fd != FD_NONE) {
        Serial.println("FEED ignored: still serving the last item");
        return FEED_REFUSED;
    }

    /* Same reasoning as Bathroom: the whole point is watching the Visitor
     * react, and that happens on the pet screen. */
    if (menu_is_open()) menu_close();

    s_fd_food = f;
    s_fd_res  = decide(f, pet_get());
    s_fd_t0   = millis();
    s_fd      = FD_DROP;

    /* A randomised, reachable spot - NOT wherever the Visitor happens to be.
     * The decision to accept was already made above, before any walking, so
     * a refusal never involves trudging over first. */
    s_fd_x = (lv_coord_t)random(FOOD_ZONE_X_MIN, FOOD_ZONE_X_MAX + 1);
    s_fd_y = (lv_coord_t)random(FOOD_ZONE_Y_MIN, FOOD_ZONE_Y_MAX + 1);
    s_fd_bitten_shown = false;

    s_fd_obj = lv_obj_create(s_room);
    lv_obj_remove_style_all(s_fd_obj);
    lv_obj_clear_flag(s_fd_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    build_food_shape(s_fd_obj, f, false);
    lv_obj_set_pos(s_fd_obj, s_fd_x, -40);

    /* Autonomous wandering would fight the scripted approach. */
    ui_pet_set_wander(false);

    Serial.printf("FEED %s -> dropping at (%d,%d), decision: %s\n",
                  care_food_name(f), (int)s_fd_x, (int)s_fd_y,
                  care_feed_result_name(s_fd_res));
    return s_fd_res;
}

static void clean_seq_tick(void);   /* defined with care_clean(), below */

/* Driven from t_anim (10 fps) - the 1 s sim tick is far too coarse for this. */
void care_anim_tick(void)
{
    clean_seq_tick();

    if (s_fd == FD_NONE) return;
    const uint32_t now = millis();
    const uint32_t el  = now - s_fd_t0;

    if (s_fd == FD_DROP) {
        float t = (float)el / (float)FOOD_DROP_MS;
        if (t >= 1.0f) {
            if (s_fd_obj) lv_obj_set_pos(s_fd_obj, s_fd_x, s_fd_y);
            if (s_fd_res == FEED_DROPPED) {
                /* Refused: it does NOT walk over. It shakes its head where
                 * it stands and the food stays put as a mess. */
                apply_feed(s_fd_food, s_fd_res);
                if (s_fd_obj) { lv_obj_del(s_fd_obj); s_fd_obj = nullptr; }
                s_fd_act_ms = FOOD_REFUSE_MS;
                s_fd_t0 = now;
                s_fd = FD_ACT;
            } else {
                /* Accepted: walk to it and stop alongside, so the Visitor
                 * appears to face what it is about to eat. */
                ui_pet_walk_to((lv_coord_t)(s_fd_x + 20 - PET_BOX_PX / 2),
                               (lv_coord_t)(s_fd_y - 110));
                s_fd_t0 = now;
                s_fd = FD_APPROACH;
            }
            return;
        }
        const float te = t * t;      /* ease-in: it falls, it does not glide */
        if (s_fd_obj) lv_obj_set_pos(s_fd_obj, s_fd_x,
                                     (lv_coord_t)(-40 + (s_fd_y + 40) * te));
        return;
    }

    if (s_fd == FD_APPROACH) {
        /* Arrived when the walk ends. The timeout is a safety bound only -
         * without it an interrupted walk would strand the sequence. */
        if (ui_pet_current() != PET_ANIM_WALK || el >= FOOD_APPROACH_MAX_MS) {
            apply_feed(s_fd_food, s_fd_res);
            s_fd_act_ms = (s_fd_res == FEED_PARTIAL) ? FOOD_EAT_MS : FOOD_EAT_MS;
            s_fd_t0 = now;
            s_fd = FD_ACT;
        }
        return;
    }

    /* FD_ACT - the food visibly goes as it is eaten */
    if (s_fd_obj && s_fd_res != FEED_DROPPED) {
        const float t = (float)el / (float)s_fd_act_ms;
        if (!s_fd_bitten_shown && t >= 0.40f) {
            s_fd_bitten_shown = true;
            build_food_shape(s_fd_obj, s_fd_food, true);   /* a bite is gone */
            lv_obj_set_pos(s_fd_obj, s_fd_x, s_fd_y);
        }
        if (t >= 0.55f) {
            lv_opa_t o = (lv_opa_t)(LV_OPA_COVER * (1.0f - (t - 0.55f) / 0.45f));
            lv_obj_set_style_opa(s_fd_obj, o, 0);
        }
    }

    if (el >= s_fd_act_ms) {
        if (s_fd_res == FEED_PARTIAL) {
            /* the recognisable half-eaten burger stays exactly where it was
             * eaten, not teleported to a floor slot */
            mess_add_at(MESS_FOOD, s_fd_food, true, s_fd_x, s_fd_y);
        }
        if (s_fd_obj) { lv_obj_del(s_fd_obj); s_fd_obj = nullptr; }
        ui_pet_set_wander(true);        /* normal wandering resumes */
        s_fd = FD_NONE;
    }
}

void care_bathroom(void)
{
    pet_state_t *p = pet_mutable();
    if (s_bath_active) return;

    if (p->bathroom < BATHROOM_MIN_TO_GO_PCT) {
        ui_bubble_say(BUBBLE_T1_REACTION, "I don't need to go!");
        Serial.printf("BATHROOM not needed (%.0f%%)\n", p->bathroom);
        return;
    }

    /* Close the menu first: the whole point of this action is watching the
     * Visitor leg it off-screen, and that happens on the pet screen. Pressing
     * a button and seeing nothing at all is what made this feel broken. */
    if (menu_is_open()) menu_close();

    s_bath_active = true;
    s_was_urgent = false;
    p->bathroom = 0.0f;
    ui_pet_play(PET_ANIM_BATHROOM);
    Serial.println("BATHROOM -> running off screen, back in ~2 s");
}

static void puff_opa_cb(void *o, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}
static void puff_grow_cb(void *o, int32_t v)
{
    lv_obj_t *p = (lv_obj_t *)o;
    lv_obj_set_size(p, v, v);
    lv_obj_set_style_radius(p, v / 2, 0);
}
static void puff_del_cb(lv_anim_t *a)
{
    lv_obj_del((lv_obj_t *)a->user_data);
}

/* A little burst of expanding, fading puffs where the mess used to be. */
static void spawn_puff(lv_coord_t x, lv_coord_t y, uint16_t delay)
{
    if (!s_room) return;
    lv_obj_t *p = shape(s_room, 8, 8, x, y, 4, 0xE6ECF5);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, p);
    lv_anim_set_exec_cb(&a, puff_grow_cb);
    lv_anim_set_values(&a, 8, 30);
    lv_anim_set_time(&a, PUFF_MS);
    lv_anim_set_delay(&a, delay);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, p);
    lv_anim_set_exec_cb(&a, puff_opa_cb);
    lv_anim_set_values(&a, LV_OPA_80, LV_OPA_TRANSP);
    lv_anim_set_time(&a, PUFF_MS);
    lv_anim_set_delay(&a, delay);
    a.user_data = p;
    lv_anim_set_ready_cb(&a, puff_del_cb);
    lv_anim_start(&a);
}

/* --- the cleaning sequence ----------------------------------------------
 * The LOGIC completes immediately - messes stop counting and stop draining
 * the moment you press the button, and the cleanliness maths is untouched.
 * Only the PRESENTATION is spread over ~1-2 s: each mess object is puffed
 * away in turn while the Visitor scoots about helping. Keeping the two
 * separate means the animation can never leave the state inconsistent if it
 * is interrupted. */

static uint8_t    s_cl_n, s_cl_i;
static uint32_t   s_cl_t0;
static bool       s_cl_active;
static lv_obj_t  *s_cl_obj[MESS_MAX];
static lv_coord_t s_cl_x[MESS_MAX], s_cl_y[MESS_MAX];

void care_clean(void)
{
    pet_state_t *p = pet_mutable();
    const uint8_t n = care_mess_count();

    /* Show the cleaning happen, rather than reporting it afterwards. */
    if (menu_is_open()) menu_close();

    /* snapshot what is on the floor, then clear the STATE at once */
    s_cl_n = 0;
    for (uint8_t i = 0; i < MESS_MAX; i++) {
        if (s_mess[i].type == MESS_NONE) continue;
        s_cl_obj[s_cl_n] = s_mess[i].obj;
        s_cl_x[s_cl_n]   = s_mess[i].x;
        s_cl_y[s_cl_n]   = s_mess[i].y;
        s_cl_n++;
        s_mess[i].type = MESS_NONE;      /* stops counting and draining now */
    }
    p->mess_count = 0;
    p->cleanliness = clamp100(p->cleanliness + CLEAN_RECOVERY_PCT);

    s_cl_i = 0;
    s_cl_t0 = millis();
    s_cl_active = true;

    ui_pet_play(PET_ANIM_CLEANING);   /* scoots, then hops happily itself */
    Serial.printf("CLEAN removing %u mess(es), cleanliness -> %.0f\n", n, p->cleanliness);
    persist_mark_dirty("cleaned");

    if (n == 0) {
        s_cl_active = false;
        ui_bubble_say(BUBBLE_T1_REACTION, "It's already clean!");
    }
}

static void clean_seq_tick(void)
{
    if (!s_cl_active) return;
    const uint32_t el = millis() - s_cl_t0;

    while (s_cl_i < s_cl_n && el >= (uint32_t)s_cl_i * CLEAN_STEP_MS) {
        lv_obj_t *o = s_cl_obj[s_cl_i];
        if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        /* a small burst right where the mess was */
        spawn_puff(s_cl_x[s_cl_i] + 4,  s_cl_y[s_cl_i] + 2,  0);
        spawn_puff(s_cl_x[s_cl_i] + 18, s_cl_y[s_cl_i] - 4,  90);
        spawn_puff(s_cl_x[s_cl_i] + 10, s_cl_y[s_cl_i] + 12, 170);
        s_cl_i++;
    }

    if (s_cl_i >= s_cl_n || el > CLEAN_SEQ_MAX_MS) {
        /* make sure nothing is left visible even if we ran out of time */
        for (uint8_t k = s_cl_i; k < s_cl_n; k++)
            if (s_cl_obj[k]) lv_obj_add_flag(s_cl_obj[k], LV_OBJ_FLAG_HIDDEN);
        s_cl_active = false;
        ui_bubble_say(BUBBLE_T1_REACTION, "All tidy now!");
    }
}

bool care_is_holding(void) { return ui_pet_current() == PET_ANIM_HOLDING; }

/* --- test hooks ---------------------------------------------------------- */


/* --- bed ----------------------------------------------------------------
 * Grows with the Visitor: a small cot for a Baby up to a proper bed for an
 * Adult. Built from primitives, sized and detailed per stage - enough that
 * it reads as "the bed grew" without needing art. */
static void build_bed(uint8_t stage)
{
    if (!s_room) return;
    if (!s_bed) {
        s_bed = lv_obj_create(s_room);
        lv_obj_remove_style_all(s_bed);
        lv_obj_clear_flag(s_bed, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_clean(s_bed);

    lv_coord_t w, h; uint32_t frame, sheet, pillow;
    switch (stage) {
        case STAGE_BABY: w = 132; h = 56; frame = 0xC98BB0; sheet = 0xF4D9E6; pillow = 0xFFFFFF; break;
        case STAGE_KID:  w = 158; h = 60; frame = 0x7FA8E8; sheet = 0xCFE0F7; pillow = 0xFFFFFF; break;
        case STAGE_TEEN: w = 182; h = 64; frame = 0x6E7B96; sheet = 0xC2CBDD; pillow = 0xF2F4F8; break;
        default:         w = 206; h = 68; frame = 0x8A6340; sheet = 0xD8C6AE; pillow = 0xFFFFFF; break;
    }

    lv_obj_set_size(s_bed, w, h);
    lv_obj_set_pos(s_bed, BED_CX - w / 2, BED_CY - h / 2);

    shape(s_bed, w, h, 0, 0, 10, frame);                     /* frame       */
    shape(s_bed, w - 14, h - 20, 7, 12, 7, sheet);           /* blanket     */
    shape(s_bed, w / 4, h - 26, 11, 8, 6, pillow);           /* pillow      */
    if (stage >= STAGE_TEEN)                                  /* headboard  */
        shape(s_bed, 8, h + 10, w - 10, -10, 4, frame);
    if (stage == STAGE_BABY) {                                /* cot bars   */
        for (int i = 0; i < 4; i++)
            shape(s_bed, 3, 14, 16 + i * 26, -12, 2, frame);
    }
    lv_obj_clear_flag(s_bed, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_bed);      /* pet is drawn in front of it */
}

static void bed_hide(void) { if (s_bed) lv_obj_add_flag(s_bed, LV_OBJ_FLAG_HIDDEN); }

/* Lights drive the real panel brightness. The daytime value is the config
 * constant, never overwritten - turning the lights back on restores it. */
static void apply_lights_brightness(void)
{
    bsp_set_brightness(s_lights_on ? BRIGHT_FULL : LIGHTS_OFF_BRIGHTNESS);
}

bool care_lights_on(void)          { return s_lights_on; }
void care_set_lights(bool on)      { s_lights_on = on; apply_lights_brightness(); }

/* --- persistence helpers -------------------------------------------------
 * Messes are saved and restored with their exact type, food, bitten state,
 * position and accumulated drain. A restored mess is the SAME mess, not a
 * fresh generic one - an old poop keeps its stink and old food keeps the
 * penalty it already took. */
void care_restore_mess(uint8_t type, uint8_t food, bool bitten,
                       uint32_t age_ms, float drained, int16_t x, int16_t y)
{
    for (uint8_t i = 0; i < MESS_MAX; i++) {
        if (s_mess[i].type != MESS_NONE) continue;
        s_mess[i].type     = (mess_type_t)type;
        s_mess[i].food     = (food_t)food;
        s_mess[i].bitten   = bitten;
        s_mess[i].age_ms   = age_ms;
        s_mess[i].drained  = drained;
        s_mess[i].stinking = (type == MESS_POOP && age_ms >= STINK_AFTER_MS);
        if (!s_mess[i].obj && s_room) {
            s_mess[i].obj = lv_obj_create(s_room);
            lv_obj_remove_style_all(s_mess[i].obj);
            lv_obj_clear_flag(s_mess[i].obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
        mess_style(&s_mess[i], i);
        if (x >= 0) {
            s_mess[i].x = x; s_mess[i].y = y;
            lv_obj_set_pos(s_mess[i].obj, x, y);
        }
        pet_mutable()->mess_count = care_mess_count();
        return;
    }
}

uint8_t care_mess_snapshot(uint8_t idx, uint8_t *type, uint8_t *food,
                           bool *bitten, uint32_t *age_ms, float *drained,
                           int16_t *x, int16_t *y)
{
    if (idx >= MESS_MAX || s_mess[idx].type == MESS_NONE) return 0;
    *type = (uint8_t)s_mess[idx].type;
    *food = (uint8_t)s_mess[idx].food;
    *bitten = s_mess[idx].bitten;
    *age_ms = s_mess[idx].age_ms;
    *drained = s_mess[idx].drained;
    *x = s_mess[idx].x; *y = s_mess[idx].y;
    return 1;
}

void care_reset(void)
{
    for (uint8_t i = 0; i < MESS_MAX; i++) {
        s_mess[i].type = MESS_NONE;
        s_mess[i].drained = 0.0f;
        s_mess[i].age_ms = 0;
        s_mess[i].stinking = false;
        if (s_mess[i].obj) lv_obj_add_flag(s_mess[i].obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_fd_obj) { lv_obj_del(s_fd_obj); s_fd_obj = nullptr; }
    s_fd = FD_NONE;
    s_cl_active = false;
    s_bath_active = false;
    s_was_urgent = false;
    s_last_ms = millis();
    ui_pet_set_wander(true);
    ui_pet_set_urgency(0.0f);
    ui_pet_play(PET_ANIM_IDLE);
    pet_mutable()->mess_count = 0;
    Serial.println("CARE reset: floor cleared, sequences cancelled");
}

void care_fast_forward(uint32_t minutes)
{
    /* Applied in one-minute steps rather than one big dt, so mess grace
     * periods and per-mess drain caps resolve exactly as they would in real
     * time. A single 6-hour dt would skip straight past the grace window. */
    Serial.printf("FAST FORWARD %lu min\n", (unsigned long)minutes);
    sim_ctx_t ctx = { pet_get()->asleep, care_lights_on(), false };
    for (uint32_t i = 0; i < minutes; i++) care_advance(60000UL, &ctx, nullptr);
    care_report();
}

void care_report(void)
{
    const pet_state_t *p = pet_get();
    Serial.println();
    Serial.println("=== CARE STATE ============================================");
    Serial.printf("  hunger %.0f  clean %.0f  happy %.0f  weight %.1f g (norm %.2f)\n",
                  p->hunger, p->cleanliness, p->happiness, p->weight_g, pet_weight_norm());
    Serial.printf("  bathroom %.0f%%  %s\n", p->bathroom,
                  p->bathroom >= BATHROOM_URGENT_PCT ? "URGENT" : "ok");
    Serial.printf("  messes %u/%u   meals %u  cakes %u  accidents %u\n",
                  care_mess_count(), MESS_MAX, p->meals, p->cakes_eaten, p->accidents);
    for (uint8_t i = 0; i < MESS_MAX; i++) {
        if (s_mess[i].type == MESS_NONE) continue;
        const bool poop = (s_mess[i].type == MESS_POOP);
        Serial.printf("    [%u] %-9s age %lus  drained %.1f / %.1f cap\n", i,
                      poop ? "accident" : "food", (unsigned long)(s_mess[i].age_ms / 1000),
                      s_mess[i].drained, poop ? MESS_POOP_DRAIN_CAP : MESS_FOOD_DRAIN_CAP);
    }
    Serial.println("-----------------------------------------------------------");
}
