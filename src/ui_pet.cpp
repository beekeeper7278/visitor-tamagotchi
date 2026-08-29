/* ===========================================================================
 * ui_pet - procedural Visitor renderer  [SPEC section 8]
 *
 * OBJECT COMPOSITION, NOT A CANVAS. See ui_pet.h for the reasoning.
 *
 * Every frame recomputes the full layout from (form + live modifiers +
 * animation offsets) and writes geometry to the existing objects. Nothing is
 * created or deleted per frame, so the LVGL heap is flat at runtime and only
 * the dirty rectangles are redrawn.
 *
 * HONEST LIMITATION, and it is the reason section 8 specifies a hybrid:
 * LVGL 8.3 primitives are rounded rectangles, circles, borders, gradients
 * and arcs. Nothing concave, and no rotation for non-image objects. So
 * EYE_STAR, EYE_SPIRAL, EYE_ANGRY_SLANT and MOUTH_WOBBLE are approximations
 * built from what primitives can express. They are the first real candidates
 * for the PNG overlay when art exists. Each one is marked APPROX below.
 * ======================================================================== */

#include <Arduino.h>
#include <math.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "forms.h"
#include "ui_pet.h"

/* --- objects ------------------------------------------------------------ */
static lv_obj_t *o_root;
static lv_obj_t *o_body, *o_belly;
static lv_obj_t *o_cheek_l, *o_cheek_r;
static lv_obj_t *o_nub_l, *o_nub_r;
static lv_obj_t *o_eye_l, *o_eye_r;           /* eye shape                  */
static lv_obj_t *o_eyx_l, *o_eyx_r;           /* highlight / lid / inner    */
static lv_obj_t *o_brow_l, *o_brow_r;
static lv_obj_t *o_mouth_a, *o_mouth_b;       /* arcs                       */
static lv_obj_t *o_mouth_fill, *o_mouth_teeth;
static lv_obj_t *o_spark[3];

/* --- state -------------------------------------------------------------- */
static uint8_t     s_form_id = FORM_BABY;
static int         s_eye_ovr = -1, s_mouth_ovr = -1, s_brow_ovr = -1;
static pet_live_t  s_live = { 0.5f, 100 };

static pet_anim_t  s_anim = PET_ANIM_IDLE;
static uint32_t    s_anim_t0 = 0;

static uint32_t    s_blink_next = 0;   /* when the next blink starts        */
static uint32_t    s_blink_t0   = 0;   /* 0 = not blinking                  */

static lv_coord_t  s_walk_from = PET_HOME_X, s_walk_to = PET_HOME_X;
static lv_coord_t  s_pos_x     = PET_HOME_X;
static lv_coord_t  s_pos_y     = PET_HOME_Y;
static lv_coord_t  s_walk_from_y = PET_HOME_Y, s_walk_to_y = PET_HOME_Y;
static uint32_t    s_wander_at  = 0;   /* next autonomous walk */
static pet_anim_done_cb_t s_done_cb = nullptr;
/* bathroom sub-phases: 0 running out, 1 away, 2 running back */
static uint8_t     s_bath_phase = 0;
static lv_coord_t  s_bath_from  = 0, s_bath_exit = 0;
static uint32_t    s_bath_started_ms = 0;

/* --- small helpers ------------------------------------------------------ */

static lv_obj_t *mk(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, uint32_t hex)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(hex), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_arc(lv_obj_t *parent)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_arc_set_rotation(a, 0);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER,  LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return a;
}

static void show(lv_obj_t *o, bool v)
{
    if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static float phase(uint32_t period_ms)
{
    return (float)(millis() % period_ms) / (float)period_ms * 6.2831853f;
}

/* --- creation ----------------------------------------------------------- */

void ui_pet_create(lv_obj_t *parent)
{
    o_root = lv_obj_create(parent);
    lv_obj_remove_style_all(o_root);
    lv_obj_set_size(o_root, PET_BOX_PX, PET_BOX_PX);
    lv_obj_set_pos(o_root, PET_HOME_X, PET_HOME_Y);
    lv_obj_clear_flag(o_root, LV_OBJ_FLAG_SCROLLABLE);
    /* clickable so a tap on the pet is distinguishable from the background */
    lv_obj_add_flag(o_root, LV_OBJ_FLAG_CLICKABLE);

    o_body        = mk(o_root, 10, 10, 0xFFFFFF);
    o_belly       = mk(o_root, 10, 10, 0xFFFFFF);
    o_nub_l       = mk(o_root, 10, 10, 0xFFFFFF);
    o_nub_r       = mk(o_root, 10, 10, 0xFFFFFF);
    o_cheek_l     = mk(o_root, 10, 10, 0xFFFFFF);
    o_cheek_r     = mk(o_root, 10, 10, 0xFFFFFF);
    o_eye_l       = mk(o_root, 10, 10, 0x000000);
    o_eye_r       = mk(o_root, 10, 10, 0x000000);
    o_eyx_l       = mk(o_root, 4,  4,  0xFFFFFF);
    o_eyx_r       = mk(o_root, 4,  4,  0xFFFFFF);
    o_brow_l      = mk(o_root, 10, 3,  0x000000);
    o_brow_r      = mk(o_root, 10, 3,  0x000000);
    o_mouth_fill  = mk(o_root, 10, 6,  0x000000);
    o_mouth_teeth = mk(o_root, 8,  3,  0xFFFFFF);
    o_mouth_a     = mk_arc(o_root);
    o_mouth_b     = mk_arc(o_root);
    for (int i = 0; i < 3; i++) {
        o_spark[i] = mk(o_root, 7, 7, 0xFFFFFF);
        lv_obj_set_style_radius(o_spark[i], 2, 0);
        show(o_spark[i], false);
    }

    s_blink_next = millis() + random(ANIM_BLINK_MIN_MS, ANIM_BLINK_MAX_MS);
    s_anim_t0 = millis();
    s_wander_at = millis() + random(IDLE_WALK_MIN_MS, IDLE_WALK_MAX_MS);
}

lv_obj_t *ui_pet_root(void) { return o_root; }

/* --- face style application --------------------------------------------
 * Each branch sets geometry AND visibility for every object it owns, so
 * switching styles never leaves a stale object from the previous style
 * visible. That is why every branch ends with an explicit show() set. */

static void apply_eye(lv_obj_t *eye, lv_obj_t *ext, int style,
                      lv_coord_t cx, lv_coord_t cy, uint8_t size,
                      const pet_form_t *f, bool inner_is_left, bool blinking)
{
    const uint32_t ec = f->c_eye;

    if (blinking) {                       /* blink beats every style */
        lv_obj_set_size(eye, size, 2);
        lv_obj_set_style_radius(eye, 1, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(ec), 0);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_set_pos(eye, cx - size / 2, cy - 1);
        show(eye, true); show(ext, false);
        return;
    }

    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(eye, lv_color_hex(ec), 0);

    switch (style) {
    case EYE_DOT: {
        const lv_coord_t d = size * 3 / 5;
        lv_obj_set_size(eye, d, d);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(eye, cx - d / 2, cy - d / 2);
        show(eye, true); show(ext, false);
        break;
    }
    case EYE_OVAL: {
        const lv_coord_t w = size, h = size * 5 / 4;
        lv_obj_set_size(eye, w, h);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(eye, cx - w / 2, cy - h / 2);
        /* highlight: what makes it read as alive rather than as a hole */
        lv_obj_set_size(ext, 4, 4);
        lv_obj_set_style_radius(ext, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ext, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(ext, cx - w / 2 + 2, cy - h / 2 + 3);
        show(eye, true); show(ext, true);
        break;
    }
    case EYE_SLEEPY: {                    /* a lowered lid, not a closed eye */
        lv_obj_set_size(eye, size, 3);
        lv_obj_set_style_radius(eye, 2, 0);
        lv_obj_set_pos(eye, cx - size / 2, cy);
        show(eye, true); show(ext, false);
        break;
    }
    case EYE_STAR: {                      /* APPROX: no star primitive */
        const lv_coord_t d = size * 6 / 5;
        lv_obj_set_size(eye, d, d);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(f->c_accent), 0);
        lv_obj_set_pos(eye, cx - d / 2, cy - d / 2);
        /* small bright core reads as a sparkle at this size */
        lv_obj_set_size(ext, 5, 5);
        lv_obj_set_style_radius(ext, 1, 0);
        lv_obj_set_style_bg_color(ext, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(ext, cx - 2, cy - 2);
        show(eye, true); show(ext, true);
        break;
    }
    case EYE_ANGRY_SLANT: {               /* APPROX: no rotation in LVGL 8.3 */
        const lv_coord_t w = size, h = size;
        lv_obj_set_size(eye, w, h);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(eye, cx - w / 2, cy - h / 2);
        /* a body-coloured lid clipping the inner-top corner fakes the slant */
        lv_obj_set_size(ext, w + 2, h / 2);
        lv_obj_set_style_radius(ext, 0, 0);
        lv_obj_set_style_bg_color(ext, lv_color_hex(f->c_body), 0);
        lv_obj_set_pos(ext, cx - w / 2 + (inner_is_left ? -2 : 0), cy - h / 2 - 1);
        show(eye, true); show(ext, true);
        break;
    }
    case EYE_SPIRAL: {                    /* APPROX: ring stands in for spiral */
        const lv_coord_t d = size * 6 / 5;
        lv_obj_set_size(eye, d, d);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(eye, 3, 0);
        lv_obj_set_style_border_color(eye, lv_color_hex(ec), 0);
        lv_obj_set_style_border_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_pos(eye, cx - d / 2, cy - d / 2);
        lv_obj_set_size(ext, 4, 4);
        lv_obj_set_style_radius(ext, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ext, lv_color_hex(ec), 0);
        lv_obj_set_pos(ext, cx - 2, cy - 2);
        show(eye, true); show(ext, true);
        break;
    }
    default:
        show(eye, false); show(ext, false);
        break;
    }
}

static void apply_mouth(int style, lv_coord_t cx, lv_coord_t cy,
                        const pet_form_t *f)
{
    const uint32_t mc = f->c_eye;
    const lv_coord_t w = 34, h = 24;

    show(o_mouth_a, false); show(o_mouth_b, false);
    show(o_mouth_fill, false); show(o_mouth_teeth, false);

    switch (style) {
    case MOUTH_SMILE:
    case MOUTH_SMIRK:
    case MOUTH_FROWN: {
        /* LVGL angles: 0 = 3 o'clock, increasing clockwise, and screen y is
         * down - so the BOTTOM arc (20..160) is the smile. */
        int a0, a1;
        if (style == MOUTH_SMILE)      { a0 = 20;  a1 = 160; }
        else if (style == MOUTH_SMIRK) { a0 = 20;  a1 = 95;  }
        else                           { a0 = 200; a1 = 340; }
        lv_obj_set_size(o_mouth_a, w, h);
        lv_obj_set_pos(o_mouth_a, cx - w / 2, cy - h / 2);
        lv_arc_set_angles(o_mouth_a, a0, a1);
        lv_obj_set_style_arc_width(o_mouth_a, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(o_mouth_a, lv_color_hex(mc), LV_PART_INDICATOR);
        show(o_mouth_a, true);
        break;
    }
    case MOUTH_FLAT:
        lv_obj_set_size(o_mouth_fill, 22, 3);
        lv_obj_set_style_radius(o_mouth_fill, 2, 0);
        lv_obj_set_style_bg_color(o_mouth_fill, lv_color_hex(mc), 0);
        lv_obj_set_pos(o_mouth_fill, cx - 11, cy);
        show(o_mouth_fill, true);
        break;

    case MOUTH_OPEN_HAPPY:
        lv_obj_set_size(o_mouth_fill, 26, 18);
        lv_obj_set_style_radius(o_mouth_fill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o_mouth_fill, lv_color_hex(mc), 0);
        lv_obj_set_pos(o_mouth_fill, cx - 13, cy - 6);
        show(o_mouth_fill, true);
        break;

    case MOUTH_TOOTHY:
        lv_obj_set_size(o_mouth_fill, 30, 16);
        lv_obj_set_style_radius(o_mouth_fill, 5, 0);
        lv_obj_set_style_bg_color(o_mouth_fill, lv_color_hex(mc), 0);
        lv_obj_set_pos(o_mouth_fill, cx - 15, cy - 5);
        lv_obj_set_size(o_mouth_teeth, 26, 5);
        lv_obj_set_style_radius(o_mouth_teeth, 1, 0);
        lv_obj_set_style_bg_color(o_mouth_teeth, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(o_mouth_teeth, cx - 13, cy - 3);
        show(o_mouth_fill, true); show(o_mouth_teeth, true);
        break;

    case MOUTH_WOBBLE: {
        /* APPROX: no path primitive, so a wavy mouth is two short arcs -
         * one curving up, one down - butted together. */
        const lv_coord_t hw = 18, hh = 16;
        lv_obj_set_size(o_mouth_a, hw, hh);
        lv_obj_set_pos(o_mouth_a, cx - hw, cy - hh / 2);
        lv_arc_set_angles(o_mouth_a, 20, 160);
        lv_obj_set_style_arc_width(o_mouth_a, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(o_mouth_a, lv_color_hex(mc), LV_PART_INDICATOR);

        lv_obj_set_size(o_mouth_b, hw, hh);
        lv_obj_set_pos(o_mouth_b, cx, cy - hh / 2);
        lv_arc_set_angles(o_mouth_b, 200, 340);
        lv_obj_set_style_arc_width(o_mouth_b, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(o_mouth_b, lv_color_hex(mc), LV_PART_INDICATOR);
        show(o_mouth_a, true); show(o_mouth_b, true);
        break;
    }
    default:
        break;
    }
}

/* --- per-frame animation offsets ---------------------------------------- */
static lv_coord_t s_off_x = 0, s_off_y = 0;
static int        s_squash = 0;      /* +ve = wider/shorter this frame      */
static int        s_anim_eye = -1;   /* animation-forced eye style          */
static int        s_anim_mouth = -1; /* animation-forced mouth style        */
static bool       s_spark_on = false;
static float      s_urgency = 0.0f;

/* --- layout -------------------------------------------------------------
 * Recomputed every frame from form + live modifiers + animation offsets.
 * The pet is anchored by its FEET to a fixed baseline inside the 160x160
 * box, so breathing and squashing change the silhouette without making it
 * appear to float. */
static void layout(void)
{
    const pet_form_t *f = forms_get(s_form_id);

    const bool hold_face = (s_anim == PET_ANIM_HOLDING);
    const int eye   = (s_anim_eye >= 0) ? s_anim_eye
                    : hold_face ? EYE_SLEEPY          /* tense, half-shut   */
                    : (s_eye_ovr  >= 0) ? s_eye_ovr  : f->eye_style;
    /* HOLDING used to force MOUTH_WOBBLE, but that style is a two-arc
     * approximation of a wavy line and it read as a glitch rather than as
     * discomfort. A small flat grimace is far more natural for "I need to
     * go" - strained rather than broken. */
    const int mouth = (s_anim_mouth >= 0) ? s_anim_mouth
                    : hold_face ? MOUTH_FLAT
                    : (s_mouth_ovr >= 0) ? s_mouth_ovr : f->mouth_style;
    const int brow  = hold_face ? BROW_WORRIED
                    : (s_brow_ovr  >= 0) ? s_brow_ovr  : f->brow_style;

    /* silhouette: squash parameter trades width against height */
    float w = f->body_w * (1.0f + f->body_squash / 200.0f);
    float h = f->body_h * (1.0f - f->body_squash / 200.0f);

    /* live weight modifier: +/-20% on width */
    w *= 1.0f + (s_live.weight_norm - 0.5f) * 2.0f * (PET_WEIGHT_SCALE_PCT / 100.0f);

    /* breathing, half speed when sad */
    const uint32_t bp = ANIM_BREATHE_PERIOD_MS * (s_anim == PET_ANIM_SAD ? 2u : 1u);
    h += ANIM_BREATHE_AMP_PX * sinf(phase(bp));

    if (s_anim == PET_ANIM_SAD) h -= ANIM_SAD_DROP_PX;

    /* HOLDING: narrower and a little taller - a body drawn in on itself. */
    if (s_anim == PET_ANIM_HOLDING) {
        w -= (lv_coord_t)(8 + 6 * s_urgency);      /* squeeze in */
        h += (lv_coord_t)(5 + 4 * s_urgency);
    }

    /* walk squash */
    w += s_squash;
    h -= s_squash;

    const lv_coord_t bw = (lv_coord_t)w, bh = (lv_coord_t)h;
    const lv_coord_t baseline = PET_BOX_PX - 8;
    const lv_coord_t bx = (PET_BOX_PX - bw) / 2;
    const lv_coord_t by = baseline - bh + s_off_y;

    /* body */
    lv_obj_set_size(o_body, bw, bh);
    lv_obj_set_pos(o_body, bx, by);
    lv_obj_set_style_radius(o_body, (bh * f->body_round) / 100, 0);
    lv_obj_set_style_bg_color(o_body, lv_color_hex(f->c_body), 0);

    /* belly: a lighter panel low on the body */
    const lv_coord_t lw = bw * 3 / 5, lh = bh * 2 / 5;
    lv_obj_set_size(o_belly, lw, lh);
    lv_obj_set_pos(o_belly, bx + (bw - lw) / 2, by + bh - lh - 4);
    lv_obj_set_style_radius(o_belly, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o_belly, lv_color_hex(f->c_belly), 0);

    const lv_coord_t fcx0 = bx + bw / 2;

    /* limbs */
    const bool nubs = (f->limb_style == LIMB_NUBS);
    const bool holding = (s_anim == PET_ANIM_HOLDING);
    if (nubs && holding) {
        /* hands down in front, held together low and centre - the readable
         * part of a "needs the bathroom" pose */
        const lv_coord_t nw = 15, nh = 12;
        lv_obj_set_size(o_nub_l, nw, nh);
        lv_obj_set_size(o_nub_r, nw, nh);
        lv_obj_set_style_radius(o_nub_l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(o_nub_r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o_nub_l, lv_color_hex(f->c_body), 0);
        lv_obj_set_style_bg_color(o_nub_r, lv_color_hex(f->c_body), 0);
        lv_obj_set_pos(o_nub_l, fcx0 - nw - 1, by + bh - nh - 8);
        lv_obj_set_pos(o_nub_r, fcx0 + 1,      by + bh - nh - 8);
    } else if (nubs) {
        const lv_coord_t nw = 16, nh = 13;
        lv_obj_set_size(o_nub_l, nw, nh);
        lv_obj_set_size(o_nub_r, nw, nh);
        lv_obj_set_style_radius(o_nub_l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(o_nub_r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o_nub_l, lv_color_hex(f->c_body), 0);
        lv_obj_set_style_bg_color(o_nub_r, lv_color_hex(f->c_body), 0);
        lv_obj_set_pos(o_nub_l, bx - nw / 2 + 3,      by + bh * 3 / 5);
        lv_obj_set_pos(o_nub_r, bx + bw - nw / 2 - 3, by + bh * 3 / 5);
    }
    show(o_nub_l, nubs); show(o_nub_r, nubs);

    /* face geometry, proportional to the body so later forms scale */
    const lv_coord_t fcx = bx + bw / 2;
    const lv_coord_t eye_y = by + bh * 2 / 5 + (s_anim == PET_ANIM_SAD ? 3 : 0);
    const lv_coord_t ex_l = fcx - f->eye_spacing / 2;
    const lv_coord_t ex_r = fcx + f->eye_spacing / 2;
    const bool blinking = (s_blink_t0 != 0);

    apply_eye(o_eye_l, o_eyx_l, eye, ex_l, eye_y, f->eye_size, f, false, blinking);
    apply_eye(o_eye_r, o_eyx_r, eye, ex_r, eye_y, f->eye_size, f, true,  blinking);

    /* brows. APPROX: no rotation for non-image objects in LVGL 8.3, so the
     * slant is expressed as an inner/outer height offset rather than a real
     * angle. Reads correctly at this size; a PNG would do it properly. */
    if (brow == BROW_NONE) {
        show(o_brow_l, false); show(o_brow_r, false);
    } else {
        const lv_coord_t brw = f->eye_size + 2;
        const lv_coord_t by0 = eye_y - f->eye_size - 4;
        const int drop = (brow == BROW_ANGRY) ? 4 : -4;
        lv_obj_set_size(o_brow_l, brw, 3);
        lv_obj_set_size(o_brow_r, brw, 3);
        lv_obj_set_style_radius(o_brow_l, 1, 0);
        lv_obj_set_style_radius(o_brow_r, 1, 0);
        lv_obj_set_style_bg_color(o_brow_l, lv_color_hex(f->c_eye), 0);
        lv_obj_set_style_bg_color(o_brow_r, lv_color_hex(f->c_eye), 0);
        lv_obj_set_pos(o_brow_l, ex_l - brw / 2, by0 + (drop > 0 ? 0 : -drop));
        lv_obj_set_pos(o_brow_r, ex_r - brw / 2, by0 + (drop > 0 ? 0 : -drop));
        show(o_brow_l, true); show(o_brow_r, true);
    }

    apply_mouth(mouth, fcx, by + bh * 3 / 5 + 4, f);

    /* cheeks */
    const bool blush = f->cheek_blush != 0;
    if (blush) {
        lv_obj_set_size(o_cheek_l, 13, 8);
        lv_obj_set_size(o_cheek_r, 13, 8);
        lv_obj_set_style_radius(o_cheek_l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(o_cheek_r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o_cheek_l, lv_color_hex(f->c_accent), 0);
        lv_obj_set_style_bg_color(o_cheek_r, lv_color_hex(f->c_accent), 0);
        lv_obj_set_style_bg_opa(o_cheek_l, LV_OPA_70, 0);
        lv_obj_set_style_bg_opa(o_cheek_r, LV_OPA_70, 0);
        lv_obj_set_pos(o_cheek_l, ex_l - f->eye_size, eye_y + f->eye_size);
        lv_obj_set_pos(o_cheek_r, ex_r - 2,           eye_y + f->eye_size);
    }
    show(o_cheek_l, blush); show(o_cheek_r, blush);

    /* sparkles: effects layer. These are the clearest future PNG candidates
     * - section 8 lists sparkle as a PNG - but primitives carry the happy
     * animation without shipping placeholder art. */
    if (s_spark_on) {
        const lv_coord_t sx[3] = { (lv_coord_t)(bx - 6), (lv_coord_t)(bx + bw), (lv_coord_t)(bx + bw / 2) };
        const lv_coord_t sy[3] = { (lv_coord_t)(by + 6), (lv_coord_t)(by + 18), (lv_coord_t)(by - 10) };
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_bg_color(o_spark[i], lv_color_hex(f->c_accent), 0);
            lv_obj_set_pos(o_spark[i], sx[i], sy[i]);
        }
    }
    for (int i = 0; i < 3; i++) show(o_spark[i], s_spark_on);

    /* finally place the whole box */
    lv_obj_set_pos(o_root, s_pos_x + s_off_x, s_pos_y);
}

/* --- animation driver (t_anim, 10 fps) ---------------------------------- */

void ui_pet_play(pet_anim_t a)
{
    if (a >= PET_ANIM_COUNT) return;

    /* ROOT-CAUSE FIX. The bathroom sequence is the only animation that hides
     * the pet and parks it off-screen, and it used to be the only code that
     * could undo that. So ANY other trigger arriving mid-sequence - feeding,
     * cleaning, a tap, a diagnostic - replaced the state machine and left the
     * pet permanently invisible off the edge of the panel, with no way back.
     *
     * Leaving the bathroom state must therefore always restore visibility and
     * a valid on-screen position, no matter who interrupted it or why. */
    if (s_anim == PET_ANIM_BATHROOM && a != PET_ANIM_BATHROOM) {
        lv_obj_clear_flag(o_root, LV_OBJ_FLAG_HIDDEN);
        if (s_pos_x < PET_ROAM_X_MIN || s_pos_x > PET_ROAM_X_MAX) s_pos_x = PET_HOME_X;
        if (s_pos_y < PET_ROAM_Y_MIN || s_pos_y > PET_ROAM_Y_MAX) s_pos_y = PET_HOME_Y;
        s_bath_phase = 0;
    }

    s_anim = a;
    s_anim_t0 = millis();

    if (a == PET_ANIM_IDLE) {
        s_wander_at = millis() + (uint32_t)random(IDLE_WALK_MIN_MS, IDLE_WALK_MAX_MS);
    }
    if (a == PET_ANIM_BATHROOM) {
        s_bath_started_ms = millis();
        s_bath_phase = 0;
        s_bath_from  = s_pos_x;
        /* leave by the nearer edge - running the full width first would read
         * as a detour rather than urgency */
        s_bath_exit  = (s_pos_x + PET_BOX_PX / 2 < BSP_LCD_W / 2)
                     ? (lv_coord_t)(-PET_BOX_PX - 4)
                     : (lv_coord_t)(BSP_LCD_W + 4);
        lv_obj_clear_flag(o_root, LV_OBJ_FLAG_HIDDEN);
    }
    if (a == PET_ANIM_WALK) {
        /* Pick a destination anywhere in the roaming rectangle, so the pet
         * explores the screen instead of pacing a single line. */
        s_walk_from   = s_pos_x;
        s_walk_from_y = s_pos_y;
        s_walk_to     = (lv_coord_t)random(PET_ROAM_X_MIN, PET_ROAM_X_MAX + 1);
        s_walk_to_y   = (lv_coord_t)random(PET_ROAM_Y_MIN, PET_ROAM_Y_MAX + 1);
    }
}

pet_anim_t ui_pet_current(void) { return s_anim; }

void ui_pet_force_blink(void) { s_blink_t0 = millis(); }
void ui_pet_set_done_cb(pet_anim_done_cb_t cb) { s_done_cb = cb; }
void ui_pet_set_urgency(float u) { s_urgency = u < 0 ? 0 : (u > 1 ? 1 : u); }

bool       ui_pet_hidden(void)     { return o_root && lv_obj_has_flag(o_root, LV_OBJ_FLAG_HIDDEN); }
lv_coord_t ui_pet_x(void)          { return s_pos_x; }
lv_coord_t ui_pet_y(void)          { return s_pos_y; }
uint8_t    ui_pet_bath_phase(void) { return s_bath_phase; }

void ui_pet_set_x(lv_coord_t x)
{
    if (x < PET_ROAM_X_MIN) x = PET_ROAM_X_MIN;
    if (x > PET_ROAM_X_MAX) x = PET_ROAM_X_MAX;
    s_pos_x = s_walk_from = s_walk_to = x;
    ui_pet_play(PET_ANIM_IDLE);
}

void ui_pet_tick(void)
{
    const uint32_t now = millis();
    const uint32_t el  = now - s_anim_t0;

    /* Blinking is independent of the animation state - it runs in every
     * state, which is what makes the pet read as alive while idle. */
    if (s_blink_t0) {
        if (now - s_blink_t0 >= ANIM_BLINK_MS) {
            s_blink_t0 = 0;
            s_blink_next = now + (uint32_t)random(ANIM_BLINK_MIN_MS, ANIM_BLINK_MAX_MS);
        }
    } else if ((int32_t)(now - s_blink_next) >= 0) {
        s_blink_t0 = now;
    }

    s_off_x = 0; s_off_y = 0; s_squash = 0;
    s_anim_eye = -1; s_anim_mouth = -1; s_spark_on = false;

    switch (s_anim) {
    case PET_ANIM_WALK: {
        float t = (float)el / (float)ANIM_WALK_MS;
        if (t >= 1.0f) {
            s_pos_x = s_walk_to;
            s_pos_y = s_walk_to_y;
            ui_pet_play(PET_ANIM_IDLE);
        } else {
            /* Ease the vertical component so diagonal moves read as an
             * amble rather than a straight-line slide. */
            const float te = t * t * (3.0f - 2.0f * t);
            s_pos_x = s_walk_from   + (lv_coord_t)((s_walk_to   - s_walk_from)   * t);
            s_pos_y = s_walk_from_y + (lv_coord_t)((s_walk_to_y - s_walk_from_y) * te);
            /* two-phase squash + bob, so it reads as steps not a slide */
            const float ph = t * 6.2831853f * 2.0f;
            s_off_y  = -(lv_coord_t)(ANIM_WALK_BOB_PX * fabsf(sinf(ph)));
            s_squash = (int)(5.0f * cosf(ph));
        }
        break;
    }
    case PET_ANIM_REACT:
        if (el >= ANIM_REACT_MS) {
            ui_pet_play(PET_ANIM_IDLE);
        } else {
            s_off_x = (lv_coord_t)random(-ANIM_REACT_JITTER_PX, ANIM_REACT_JITTER_PX + 1);
            s_anim_eye = EYE_ANGRY_SLANT;
        }
        break;

    case PET_ANIM_HAPPY: {
        const uint32_t total = ANIM_HOP_MS * 2;
        if (el >= total) {
            ui_pet_play(PET_ANIM_IDLE);
        } else {
            const float ph = (float)(el % ANIM_HOP_MS) / (float)ANIM_HOP_MS;
            s_off_y = -(lv_coord_t)(ANIM_HOP_HEIGHT_PX * sinf(ph * 3.14159265f));
            s_spark_on = true;
        }
        break;
    }
    case PET_ANIM_HOLDING: {
        /* Restless fidget that tightens with urgency: a slow shift early on,
         * a visible squeeze-and-wiggle when it is nearly too late. Stillness
         * would not read as needing anything. */
        const uint32_t period = (uint32_t)(700 - 300 * s_urgency);
        const float f = sinf(phase(period));
        const float amp = 2.0f + 3.5f * s_urgency;
        s_off_x = (lv_coord_t)(amp * f);
        s_off_y = (lv_coord_t)((1.5f + 2.0f * s_urgency) * fabsf(f));
        break;
    }

    case PET_ANIM_CLEANING: {
        /* busy little scoot - the Visitor helping tidy up */
        const float t = (float)el / 1200.0f;
        s_off_x  = (lv_coord_t)(5.0f * sinf(t * 6.2831853f * 3.0f));
        s_squash = (int)(3.0f * sinf(t * 6.2831853f * 6.0f));
        s_anim_mouth = MOUTH_SMILE;
        if (el >= 1200) ui_pet_play(PET_ANIM_HAPPY);
        break;
    }

    case PET_ANIM_BATHROOM: {
        /* SAFETY NET. The pet is hidden and off-screen during this sequence,
         * so any way of getting stuck here means a permanently missing pet -
         * the worst failure this renderer can have. Bound the whole thing in
         * wall-clock terms and recover unconditionally, whatever the cause. */
        if (now - s_bath_started_ms > BATHROOM_TOTAL_MAX_MS) {
            Serial.printf("PET bathroom watchdog fired in phase %u - recovering\n",
                          s_bath_phase);
            s_pos_x = PET_HOME_X;
            s_pos_y = PET_HOME_Y;
            lv_obj_clear_flag(o_root, LV_OBJ_FLAG_HIDDEN);
            ui_pet_play(PET_ANIM_IDLE);
            if (s_done_cb) s_done_cb(PET_ANIM_BATHROOM);
            break;
        }
        if (s_bath_phase == 0) {
            float t = (float)el / (float)BATHROOM_RUNOFF_MS;
            if (t >= 1.0f) {
                s_pos_x = s_bath_exit;
                lv_obj_add_flag(o_root, LV_OBJ_FLAG_HIDDEN);
                s_bath_phase = 1;
                s_anim_t0 = now;
            } else {
                s_pos_x = s_bath_from + (lv_coord_t)((s_bath_exit - s_bath_from) * t);
                /* faster bob than a walk: it is running, not strolling */
                s_off_y = -(lv_coord_t)(6.0f * fabsf(sinf(t * 6.2831853f * 4.0f)));
            }
        } else if (s_bath_phase == 1) {
            if (el >= BATHROOM_AWAY_MS) {
                s_bath_phase = 2;
                s_anim_t0 = now;
                lv_obj_clear_flag(o_root, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            float t = (float)el / (float)BATHROOM_RETURN_MS;
            if (t >= 1.0f) {
                s_pos_x = PET_HOME_X;
                ui_pet_play(PET_ANIM_IDLE);
                if (s_done_cb) s_done_cb(PET_ANIM_BATHROOM);
            } else {
                s_pos_x = s_bath_exit + (lv_coord_t)((PET_HOME_X - s_bath_exit) * t);
                s_off_y = -(lv_coord_t)(6.0f * fabsf(sinf(t * 6.2831853f * 4.0f)));
            }
        }
        break;
    }

    case PET_ANIM_EATING: {
        /* mouth alternates open/closed at FOOD_CHEW_MS [SPEC section 8] */
        const bool open = ((el / FOOD_CHEW_MS) % 2) == 0;
        s_anim_mouth = open ? MOUTH_OPEN_HAPPY : MOUTH_FLAT;
        s_off_y = (lv_coord_t)(1.5f * sinf(phase(FOOD_CHEW_MS * 2)));
        if (el >= FOOD_EAT_MS) ui_pet_play(PET_ANIM_IDLE);
        break;
    }

    case PET_ANIM_REFUSE: {
        /* head shake. The Baby's head is merged with its body, so turning
         * the whole body left-right IS the head shake at this form. */
        const float t = (float)el / (float)FOOD_REFUSE_MS;
        s_off_x = (lv_coord_t)(FOOD_SHAKE_PX *
                               sinf(t * 6.2831853f * (float)FOOD_SHAKES));
        s_anim_mouth = MOUTH_FLAT;
        if (el >= FOOD_REFUSE_MS) ui_pet_play(PET_ANIM_IDLE);
        break;
    }

    case PET_ANIM_IDLE:
        /* wander off on its own schedule */
        if (s_wander_at && (int32_t)(now - s_wander_at) >= 0) {
            ui_pet_play(PET_ANIM_WALK);
        }
        break;

    default:
        break;      /* SAD is a pure layout state */
    }

    layout();
}

/* --- public setters ----------------------------------------------------- */

void ui_pet_set_form(uint8_t form_id)
{
    if (form_id < FORM_COUNT) s_form_id = form_id;
}
uint8_t ui_pet_get_form(void) { return s_form_id; }

void ui_pet_set_face(int eye_style, int mouth_style, int brow_style)
{
    s_eye_ovr   = eye_style;
    s_mouth_ovr = mouth_style;
    s_brow_ovr  = brow_style;
}

void ui_pet_set_live(const pet_live_t *live) { if (live) s_live = *live; }
void ui_pet_get_live(pet_live_t *out)        { if (out)  *out = s_live;  }

void ui_pet_anchor(lv_coord_t *x, lv_coord_t *top_y)
{
    if (x)     *x     = s_pos_x + PET_BOX_PX / 2;
    if (top_y) *top_y = s_pos_y + 10;
}

const char *ui_pet_anim_name(pet_anim_t a)
{
    switch (a) {
        case PET_ANIM_IDLE:  return "idle";
        case PET_ANIM_WALK:  return "walk";
        case PET_ANIM_REACT: return "react";
        case PET_ANIM_HAPPY: return "happy";
        case PET_ANIM_SAD:   return "sad";
        case PET_ANIM_HOLDING:  return "holding";
        case PET_ANIM_BATHROOM: return "bathroom";
        case PET_ANIM_EATING:   return "eating";
        case PET_ANIM_REFUSE:   return "refuse";
        case PET_ANIM_CLEANING: return "cleaning";
        default:             return "?";
    }
}

const char *ui_pet_eye_name(int s)
{
    switch (s) {
        case EYE_DOT:         return "DOT";
        case EYE_OVAL:        return "OVAL";
        case EYE_SLEEPY:      return "SLEEPY";
        case EYE_STAR:        return "STAR (approx)";
        case EYE_ANGRY_SLANT: return "ANGRY_SLANT (approx)";
        case EYE_SPIRAL:      return "SPIRAL (approx)";
        default:              return "?";
    }
}

const char *ui_pet_mouth_name(int s)
{
    switch (s) {
        case MOUTH_SMILE:      return "SMILE";
        case MOUTH_SMIRK:      return "SMIRK";
        case MOUTH_FLAT:       return "FLAT";
        case MOUTH_FROWN:      return "FROWN";
        case MOUTH_OPEN_HAPPY: return "OPEN_HAPPY";
        case MOUTH_TOOTHY:     return "TOOTHY";
        case MOUTH_WOBBLE:     return "WOBBLE (approx)";
        default:               return "?";
    }
}
