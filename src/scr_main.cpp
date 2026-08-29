/* scr_main - the resident pet screen. See scr_main.h for the object tree. */

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "strings.h"
#include "scr_main.h"
#include "menu.h"
#include "pet.h"
#include "care.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_room_layer;
static lv_obj_t *s_pet_layer;
static lv_obj_t *s_pet_overlay;
static lv_obj_t *s_hud_mood;
static lv_obj_t *s_btn_menu;

/* Tapping the pet is a reaction; tapping the background is not. Routing both
 * through the pet's own object rather than the screen is what keeps that
 * distinction honest once the Phase 3 menu handle is added below. */
static void menu_btn_cb(lv_event_t *e) { (void)e; menu_open(); }

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

    /* TRUE BLACK. On an AMOLED a #000000 pixel is an unlit pixel, so this is
     * the one background that costs no power and cannot burn in - and the
     * pet's mint body reads far more vividly against it than against the
     * previous near-black gradient. */
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_NONE, 0);
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

    /* hud_top. Only the mood portion is real: the clock needs the RTC and the
     * battery needs the AXP2101, which is gated at BSP_PMIC_VERIFIED 0 under
     * a standing no-reads-no-writes policy. A fabricated battery icon would
     * be worse than an empty slot. */
    /* Mood as a small coloured dot rather than a word. The word was reading
     * as a debug readout on the pet's own screen; a dot conveys the same
     * thing at a glance without competing with the Visitor for attention. */
    s_hud_mood = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_hud_mood);
    lv_obj_set_size(s_hud_mood, 12, 12);
    lv_obj_align(s_hud_mood, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_set_style_radius(s_hud_mood, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_hud_mood, LV_OPA_70, 0);
    lv_obj_clear_flag(s_hud_mood, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* btn_menu: a permanent on-screen handle. SPEC section 4 is explicit that
     * touch-only must be a complete path - a small kid will never find a
     * hardware button. */
    /* A small icon in the top-right corner. The 44x44 touch target is kept
     * even though the glyph is smaller - the visual can be subtle, the thing
     * a five-year-old has to hit cannot be. Still a complete touch-only path
     * to the menu, which SPEC section 4 requires. */
    s_btn_menu = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_btn_menu);
    lv_obj_set_size(s_btn_menu, 44, 44);
    lv_obj_align(s_btn_menu, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_radius(s_btn_menu, 12, 0);
    lv_obj_set_style_bg_color(s_btn_menu, lv_color_hex(0x14141C), 0);
    lv_obj_set_style_bg_opa(s_btn_menu, LV_OPA_60, 0);
    lv_obj_add_flag(s_btn_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_btn_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btn_menu, menu_btn_cb, LV_EVENT_CLICKED, NULL);
    /* three bars, drawn from primitives so no PNG is needed */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *bar = lv_obj_create(s_btn_menu);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 20, 3);
        lv_obj_set_pos(bar, 12, 14 + i * 7);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xC8CCD6), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    care_init(s_room_layer);
}

void scr_main_show(void)   { if (s_scr) lv_scr_load(s_scr); }
lv_obj_t *scr_main_obj(void)     { return s_scr; }
lv_obj_t *scr_main_overlay(void) { return s_pet_overlay; }

lv_obj_t *scr_main_room(void) { return s_room_layer; }

/* Mood -> colour. Deliberately low-contrast against true black: this is a
 * peripheral cue, not a readout. */
void scr_main_hud_refresh(void)
{
    if (!s_hud_mood) return;
    uint32_t c;
    switch (pet_mood()) {
        case MOOD_ASLEEP:  c = 0x4A5A8A; break;
        case MOOD_SLEEPY:  c = 0x6A7AA8; break;
        case MOOD_HUNGRY:  c = 0xE8A33D; break;
        case MOOD_YUCKY:   c = 0x9E8A5A; break;
        case MOOD_GRUMPY:  c = 0xD1656B; break;
        case MOOD_EXCITED: c = 0x60D0A0; break;
        case MOOD_PLAYFUL: c = 0x7FA8E8; break;
        default:           c = 0x5E6470; break;   /* Content: near-invisible */
    }
    lv_obj_set_style_bg_color(s_hud_mood, lv_color_hex(c), 0);
}
