#include <Arduino.h>
#include <lvgl.h>
#include "board_pins.h"
#include "config.h"
#include "bsp.h"
#include "ui_diag.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_touch_dot;
static lv_obj_t *s_touch_lbl;

static void touch_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);

    lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_touch_dot, p.x - 12, p.y - 12);

    lv_label_set_text_fmt(s_touch_lbl, "touch  x=%d  y=%d", (int)p.x, (int)p.y);
    Serial.printf("TOUCH x=%3d y=%3d\n", (int)p.x, (int)p.y);
    (void)e;
}

/* A 24x24 marker inset 2px from each corner. All four must be fully
 * visible and equally inset. If the left and right insets differ, the
 * column offset is wrong. */
static void corner(lv_coord_t x, lv_coord_t y, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(s_scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 24, 24);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

static void swatch(lv_coord_t x, lv_coord_t y, lv_color_t c, const char *name)
{
    lv_obj_t *o = lv_obj_create(s_scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 76, 56);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *l = lv_label_create(s_scr);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_pos(l, x + 4, y + 60);
}

void ui_diag_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *scr = s_scr;
    /* Dark background: AMOLED-friendly and lower power, per the burn-in
     * strategy. Never pure white anywhere in this project. */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101018), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "VISITOR  -  Phase 1");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8E8E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "368x448  CO5300  CST820");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8890A0), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 76);

    /* RGB swatches: with LV_COLOR_16_SWAP=0 these MUST read red, green,
     * blue left to right. If red and blue are swapped, the byte order is
     * wrong. This is the cheapest possible check for it. */
    swatch(30,  120, lv_color_hex(0xFF0000), "RED");
    swatch(146, 120, lv_color_hex(0x00FF00), "GREEN");
    swatch(262, 120, lv_color_hex(0x0000FF), "BLUE");

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "booting...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xB0B8C8), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status, BSP_LCD_W - 40);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 40);

    s_touch_lbl = lv_label_create(scr);
    lv_label_set_text(s_touch_lbl, "tap anywhere");
    lv_obj_set_style_text_font(s_touch_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_touch_lbl, lv_color_hex(0x60D0A0), 0);
    lv_obj_align(s_touch_lbl, LV_ALIGN_BOTTOM_MID, 0, -40);

    s_touch_dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_touch_dot);
    lv_obj_set_size(s_touch_dot, 24, 24);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0xFFD040), 0);
    lv_obj_set_style_bg_opa(s_touch_dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);

    corner(2, 2,                          lv_color_hex(0xFF00FF));
    corner(BSP_LCD_W - 26, 2,             lv_color_hex(0xFF00FF));
    corner(2, BSP_LCD_H - 26,             lv_color_hex(0x00FFFF));
    corner(BSP_LCD_W - 26, BSP_LCD_H - 26, lv_color_hex(0x00FFFF));
}

void ui_diag_set_status(const char *txt)
{
    if (s_status) lv_label_set_text(s_status, txt);
}

void ui_diag_show(void)       { if (s_scr) lv_scr_load(s_scr); }
lv_obj_t *ui_diag_screen(void) { return s_scr; }
