/* scr_main - the resident pet screen. See scr_main.h for the object tree. */

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "strings.h"
#include "scr_main.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_room_layer;
static lv_obj_t *s_pet_layer;
static lv_obj_t *s_pet_overlay;

/* Tapping the pet is a reaction; tapping the background is not. Routing both
 * through the pet's own object rather than the screen is what keeps that
 * distinction honest once the Phase 3 menu handle is added below. */
static void pet_pressed_cb(lv_event_t *e)
{
    (void)e;
    ui_pet_play(PET_ANIM_REACT);
    ui_bubble_say(BUBBLE_T1_REACTION, strings_random(BUBBLE_T1_REACTION));
}

/* Idle chatter. This only ATTEMPTS a tier-3 line; the bubble manager's
 * cooldowns refuse most of them, which is exactly the intended behaviour -
 * the pet murmurs occasionally rather than nagging. */
static void chatter_cb(lv_timer_t *t)
{
    (void)t;
    if (ui_pet_current() != PET_ANIM_IDLE) return;
    ui_bubble_say(BUBBLE_T3_IDLE, strings_random(BUBBLE_T3_IDLE));
}

void scr_main_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* bg: a dark vertical gradient. Never pure white anywhere in this
     * project - AMOLED burn-in strategy, and it is easier on the eyes in a
     * dark bedroom, which is where this device will actually live. */
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x141420), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(0x0B0B12), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    /* room_layer: empty in Phase 2. The lights-off dim overlay and the mess
     * sprite pool land with care mechanics. */
    s_room_layer = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_room_layer);
    lv_obj_set_size(s_room_layer, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_room_layer, 0, 0);
    lv_obj_clear_flag(s_room_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* pet_layer -> the primitives */
    s_pet_layer = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_pet_layer);
    lv_obj_set_size(s_pet_layer, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_pet_layer, 0, 0);
    lv_obj_clear_flag(s_pet_layer, LV_OBJ_FLAG_SCROLLABLE);
    ui_pet_create(s_pet_layer);
    lv_obj_add_event_cb(ui_pet_root(), pet_pressed_cb, LV_EVENT_PRESSED, NULL);

    /* pet_overlay: THE PNG SEAM, wired and deliberately empty.
     * Section 8's hybrid exists because LVGL 8.3 cannot draw concave shapes
     * - ears, tails, accessories. The Baby form needs none of them, so
     * Phase 2 ships no art rather than placeholder art. The layer exists now
     * so that adding an ear later is an lv_img_create() into this parent,
     * not a restructure of the renderer. */
    s_pet_overlay = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_pet_overlay);
    lv_obj_set_size(s_pet_overlay, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_pet_overlay, 0, 0);
    lv_obj_clear_flag(s_pet_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* bubble sits above everything so it is never occluded by the pet */
    ui_bubble_create(s_scr);

    lv_timer_create(chatter_cb, IDLE_CHATTER_MS, NULL);

    /* hud_top and btn_menu: Phase 3. */
}

void scr_main_show(void)   { if (s_scr) lv_scr_load(s_scr); }
lv_obj_t *scr_main_obj(void)     { return s_scr; }
lv_obj_t *scr_main_overlay(void) { return s_pet_overlay; }
