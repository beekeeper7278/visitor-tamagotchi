#pragma once
/* ===========================================================================
 * ui_pet - the procedural Visitor renderer  [SPEC section 8]
 *
 * Composes the pet from ~15 styled lv_obj primitives rather than an
 * lv_canvas. A 160x160 TRUE_COLOR_ALPHA canvas would be 102 KB and would
 * force a full blit every frame; the primitives cost ~2 KB of LVGL heap and
 * let LVGL's dirty-rect tracking redraw a 24x24 region for a blink.
 *
 * Everything is driven by pet_form_t. Nothing about the Baby is hardcoded,
 * so the later forms are a table row rather than a new renderer.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>
#include "forms.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PET_ANIM_IDLE = 0,   /* breathe + blink; the resting state             */
    PET_ANIM_WALK,       /* x lerp to target, 2-phase squash, bob          */
    PET_ANIM_REACT,      /* body jitter, angry eyes                        */
    PET_ANIM_HAPPY,      /* two hops + sparkle                             */
    PET_ANIM_SAD,        /* lower body, eyes down, half-speed breathe      */
    PET_ANIM_HOLDING,    /* needs the bathroom: hands down in front, fidget */
    PET_ANIM_BATHROOM,   /* runs off-screen, ~2 s away, returns relieved    */
    PET_ANIM_EATING,     /* mouth open/close while food is consumed         */
    PET_ANIM_REFUSE,     /* head shake "no" - won't eat / can't finish      */
    PET_ANIM_CLEANING,   /* joins in: a busy little side-to-side scoot      */
    PET_ANIM_SLEEPING,   /* settled in bed: sleepy eyes, slow breathing     */
    PET_ANIM_EVOLVING,   /* centre, shrink, flash, grow into the new form   */
    PET_ANIM_COUNT
} pet_anim_t;

/* Live modifiers applied over the form at draw time. Phase 2 sets these
 * only from the test command - nothing computes them yet (weight is Phase 4,
 * cleanliness is Phase 4). The renderer honouring them now is what makes
 * "12 forms x states, not 12 static pictures" true later. */
typedef struct {
    float   weight_norm;    /* 0..1, 0.5 = neutral; scales body_w +/-20%   */
    uint8_t cleanliness;    /* 0..100; gates the CRUMBS/STINK deco         */
} pet_live_t;

void ui_pet_create(lv_obj_t *parent);

/* Swap the form. Safe to call at any time; relayouts on the next frame. */
void ui_pet_set_form(uint8_t form_id);
uint8_t ui_pet_get_form(void);

/* Override the form's face. Pass -1 to fall back to the form's own value.
 * Expression is presentation, not mood: mood is computed in Phase 4/5 and
 * will drive this from above. */
void ui_pet_set_face(int eye_style, int mouth_style, int brow_style);

void ui_pet_set_live(const pet_live_t *live);
void ui_pet_get_live(pet_live_t *out);

/* Start an animation. IDLE is resumed automatically when a one-shot ends.
 * WALK picks its own on-screen target. */
void ui_pet_play(pet_anim_t a);
pet_anim_t ui_pet_current(void);
const char *ui_pet_anim_name(pet_anim_t a);

/* Force a blink now. Blinking is otherwise autonomous (random 3-7 s), which
 * makes it awkward to verify deliberately - hence this test hook. */
void ui_pet_force_blink(void);

/* Driven from t_anim at 10 fps. */
void ui_pet_tick(void);

/* TEST ONLY: park the pet at an exact x so bubble edge-clamping can be
 * exercised at the extremes rather than waiting for a wander to get there. */
void ui_pet_set_x(lv_coord_t x);

/* Called when a ONE-SHOT animation finishes and the pet returns to idle.
 * The bathroom relief bubble needs to fire on return, not on departure, and
 * polling for that from care.cpp would duplicate the state machine. */
typedef void (*pet_anim_done_cb_t)(pet_anim_t finished);
void ui_pet_set_done_cb(pet_anim_done_cb_t cb);

/* 0..1 urgency, drives how hard the holding pose squeezes and wiggles. */
void ui_pet_set_urgency(float u);

/* Directed walk to an exact spot, reusing the normal walk animation. Used
 * for approaching food; the destination is clamped to the roaming area so
 * the pet can never be sent off-screen. */
void ui_pet_walk_to(lv_coord_t x, lv_coord_t y);

/* Place instantly, without a walk and without changing the animation. Used
 * when bedtime is already underway and strolling over would be wrong. */
void ui_pet_place(lv_coord_t x, lv_coord_t y);

/* Gate autonomous wandering, so a scripted walk is not fighting a random
 * one. Always re-enable it when the script finishes. */
void ui_pet_set_wander(bool on);

/* Run the transformation. The form switch happens at the flash, so the old
 * shape is never seen morphing into the new one. Calls the done-callback
 * when it finishes and returns to idle. */
void ui_pet_evolve_to(uint8_t new_form);
bool ui_pet_evolving(void);

/* Egg mode: the shell is drawn instead of the Visitor. Colour is cosmetic. */
void ui_pet_set_egg(bool on, uint8_t palette);

/* Drop the shell from the selector position to the Visitor's home spot once
 * the countdown starts. Layout only - it moves nothing but pixels. Pass
 * animate=false to place it there directly (a boot that resumes a hatch). */
void ui_pet_egg_drop(bool animate);
bool ui_pet_is_egg(void);

/* 0..1 progress toward hatching. Drives how often the egg twitches - rare
 * at the start, almost constant just before it opens. */
void ui_pet_set_egg_progress(float p);

/* The Baby inherits its colour from the shell it hatched out of. Pass -1 for
 * no tint (the form table's own colours). Appearance ONLY - nothing here
 * reaches the evolution accumulators. */
void ui_pet_set_baby_palette(int idx);

/* Centre of the pet in screen coords - the bubble anchors to this. */
void ui_pet_anchor(lv_coord_t *x, lv_coord_t *top_y);

/* The pet's own root, so a tap on the pet can be distinguished from a tap
 * on the background. */
lv_obj_t *ui_pet_root(void);

/* Diagnostics: the bathroom sequence hides and moves the pet off-screen, so
 * these exist to answer "where did it go" without guessing. */
bool        ui_pet_hidden(void);
lv_coord_t  ui_pet_x(void);
lv_coord_t  ui_pet_y(void);
uint8_t     ui_pet_bath_phase(void);

const char *ui_pet_eye_name(int style);
const char *ui_pet_mouth_name(int style);

#ifdef __cplusplus
}
#endif
