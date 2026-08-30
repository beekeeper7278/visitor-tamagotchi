/* menu - six-page looping pager. See menu.h. */

#include <Arduino.h>
#include <stdlib.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "pages.h"
#include "menu.h"
#include "ui_bubble.h"
#include "scr_main.h"
#include "pages.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_cont;                 /* pages live here                 */
static lv_obj_t *s_page;                 /* the one current page            */
static lv_obj_t *s_dot[PAGE_COUNT];
static uint8_t   s_idx;
static bool      s_open;
static bool      s_busy;                 /* transition in flight            */
static bool      s_metrics;
/* Runtime, not compile-time: you can only pick the best-looking transition by
 * looking at the panel, and reflashing between each comparison makes that a
 * chore instead of a glance. config.h supplies the default. */
static uint8_t   s_tr = PAGE_TRANSITION;

/* gesture state for the current press */
static bool       s_pressed;
static bool       s_swiped;
static lv_point_t s_p0;
static uint32_t   s_t0;

bool menu_is_open(void)      { return s_open; }
uint8_t menu_page(void)      { return s_idx; }
bool menu_swipe_active(void) { return s_swiped; }
void menu_set_metrics(bool on) { s_metrics = on; }
bool menu_metrics(void)        { return s_metrics; }

const char *menu_transition_name(void)
{
    switch (s_tr) {
        case PAGE_TR_SLIDE: return "SLIDE";
        case PAGE_TR_FADE:  return "FADE";
        default:            return "CUT";
    }
}

uint8_t menu_transition(void)          { return s_tr; }
void    menu_set_transition(uint8_t t) { if (t <= PAGE_TR_CUT) s_tr = t; }

/* --- indicator dots -----------------------------------------------------
 * Pinned to the screen, OUTSIDE the animated container, so they never move
 * with the pages [SPEC section 4]. */
static void dots_refresh(void)
{
    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        const bool on = (i == s_idx);
        lv_obj_set_style_bg_color(s_dot[i],
            lv_color_hex(on ? 0xE8E8E8 : 0x3A3A46), 0);
        lv_obj_set_style_transform_zoom(s_dot[i], on ? 320 : 256, 0);
    }
}

static void dots_build(void)
{
    const lv_coord_t total = PAGE_COUNT * DOT_SIZE_PX + (PAGE_COUNT - 1) * DOT_GAP_PX;
    lv_coord_t x = (BSP_LCD_W - total) / 2;
    const lv_coord_t y = BSP_LCD_H - DOT_BOTTOM_MARGIN - DOT_SIZE_PX;

    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(s_scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_SIZE_PX, DOT_SIZE_PX);
        lv_obj_set_pos(d, x, y);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_dot[i] = d;
        x += DOT_SIZE_PX + DOT_GAP_PX;
    }
    dots_refresh();
}

/* --- transitions --------------------------------------------------------- */

static void anim_x_cb(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, (lv_coord_t)v); }
static void anim_opa_cb(void *o, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}

static void drop_old_cb(lv_anim_t *a)
{
    lv_obj_t *old = (lv_obj_t *)a->user_data;
    if (old) { lv_anim_del(old, NULL); lv_obj_del(old); }
    s_busy = false;
}

static void flash_done_cb(lv_anim_t *a) { (void)a; s_busy = false; }

static void transition_to(uint8_t next, int8_t dir)
{
    if (s_busy) return;
    lv_obj_t *old = s_page;
    s_idx = next;

    if (s_tr == PAGE_TR_SLIDE) {
        s_page = page_create(s_idx, s_cont);
        lv_obj_set_pos(s_page, dir > 0 ? BSP_LCD_W : -BSP_LCD_W, 0);
        s_busy = true;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_page);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_values(&a, dir > 0 ? BSP_LCD_W : -BSP_LCD_W, 0);
        lv_anim_set_time(&a, PAGE_SLIDE_MS);
        lv_anim_start(&a);

        lv_anim_init(&a);
        lv_anim_set_var(&a, old);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_values(&a, 0, dir > 0 ? -BSP_LCD_W : BSP_LCD_W);
        lv_anim_set_time(&a, PAGE_SLIDE_MS);
        a.user_data = old;
        lv_anim_set_ready_cb(&a, drop_old_cb);
        lv_anim_start(&a);

    } else if (s_tr == PAGE_TR_FADE) {
        s_page = page_create(s_idx, s_cont);
        lv_obj_set_pos(s_page, 0, 0);
        lv_obj_set_style_opa(s_page, LV_OPA_TRANSP, 0);
        s_busy = true;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_page);
        lv_anim_set_exec_cb(&a, anim_opa_cb);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&a, PAGE_FADE_MS);
        lv_anim_start(&a);

        lv_anim_init(&a);
        lv_anim_set_var(&a, old);
        lv_anim_set_exec_cb(&a, anim_opa_cb);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&a, PAGE_FADE_MS);
        a.user_data = old;
        lv_anim_set_ready_cb(&a, drop_old_cb);
        lv_anim_start(&a);

    } else {
        /* CUT: one redraw, no animated full-screen compositing. At the ~19.7
         * fps this panel measured, a 250 ms slide is only about five frames
         * and a 120 ms fade barely two - so the cheap option is not
         * obviously the worse-looking one here. The dot flash is what stops
         * it reading as a glitch. */
        if (old) { lv_anim_del(old, NULL); lv_obj_del(old); }
        s_page = page_create(s_idx, s_cont);
        lv_obj_set_pos(s_page, 0, 0);
        s_busy = true;

        dots_refresh();
        lv_obj_set_style_bg_color(s_dot[s_idx], lv_color_hex(0xFFFFFF), 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_dot[s_idx]);
        lv_anim_set_exec_cb(&a, anim_opa_cb);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_COVER);
        lv_anim_set_time(&a, PAGE_CUT_FLASH_MS);
        lv_anim_set_ready_cb(&a, flash_done_cb);
        lv_anim_start(&a);
    }

    dots_refresh();
    Serial.printf("PAGE -> %u %s (%s)\n", s_idx, page_name(s_idx), menu_transition_name());
}

void menu_rebuild_page(void)
{
    if (!s_open || s_busy || !s_page) return;
    lv_obj_t *old = s_page;
    s_page = page_create(s_idx, s_cont);
    lv_obj_set_pos(s_page, 0, 0);
    lv_anim_del(old, NULL);
    lv_obj_del(old);
}

void menu_goto(uint8_t page)
{
    if (!s_open || page >= PAGE_COUNT || page == s_idx) return;
    transition_to(page, (int8_t)(page > s_idx ? 1 : -1));
}

void menu_step(int8_t dir)
{
    if (!s_open || s_busy) return;
    /* mod PAGE_COUNT, so 6->1 and 1->6 wrap for free */
    const uint8_t next = (uint8_t)((s_idx + PAGE_COUNT + dir) % PAGE_COUNT);
    transition_to(next, dir);
}

/* --- gestures [SPEC section 4] -------------------------------------------
 * Tap   : released within TAP_MAX_MS and total travel < TAP_MAX_TRAVEL_PX
 * Swipe : |dx| >= SWIPE_MIN_TRAVEL_PX and |dx| > SWIPE_AXIS_RATIO * |dy|,
 *         committed MID-DRAG the instant the threshold is crossed so it
 *         feels immediate rather than waiting for release.
 * All three thresholds are [GUESS] pending the hardware tuning pass, which
 * is what menu_set_metrics() exists for. */
static void gesture_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_pressed = true; s_swiped = false;
        s_p0 = p; s_t0 = millis();
        return;
    }
    if (!s_pressed) return;

    const int dx = p.x - s_p0.x, dy = p.y - s_p0.y;

    if (code == LV_EVENT_PRESSING) {
        /* The Journal scrolls vertically inside the pager. A drag there must
         * clear a STRICTER horizontal bar, otherwise a slightly-diagonal
         * scroll flicks the page sideways mid-read. Everywhere else keeps the
         * original thresholds. */
        const bool in_scroller = (s_idx == PAGE_JOURNAL);
        const int min_dx = in_scroller ? (SWIPE_MIN_TRAVEL_PX * 3 / 2)
                                       : SWIPE_MIN_TRAVEL_PX;
        const int ratio  = in_scroller ? (SWIPE_AXIS_RATIO * 2) : SWIPE_AXIS_RATIO;
        if (!s_swiped && !s_busy &&
            abs(dx) >= min_dx &&
            abs(dx) > ratio * abs(dy)) {
            s_swiped = true;
            /* drag left  (dx<0) -> next page */
            menu_step(dx < 0 ? 1 : -1);
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const uint32_t dur = millis() - s_t0;
        const int travel = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
        const bool tap = !s_swiped && dur <= TAP_MAX_MS && travel <= TAP_MAX_TRAVEL_PX;

        if (s_metrics) {
            Serial.printf("GESTURE dur=%4lums dx=%+4d dy=%+4d travel=%3d  -> %s\n",
                          (unsigned long)dur, dx, dy, travel,
                          s_swiped ? "SWIPE" : (tap ? "tap" : "(neither)"));
            if (!s_swiped && !tap)
                Serial.printf("        thresholds: tap<=%dms/<=%dpx  swipe>=%dpx & |dx|>%d*|dy|\n",
                              TAP_MAX_MS, TAP_MAX_TRAVEL_PX,
                              SWIPE_MIN_TRAVEL_PX, SWIPE_AXIS_RATIO);
        }
        s_pressed = false;
        /* s_swiped intentionally survives until the next PRESSED, so a
         * CLICKED delivered after this release can still be rejected. */
    }
}

static void close_cb(lv_event_t *e) { (void)e; menu_close(); }

/* --- open / close -------------------------------------------------------- */

void menu_open(void)
{
    if (s_open) return;

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0B0B12), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);

    s_cont = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_size(s_cont, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_cont, 0, 0);
    lv_obj_clear_flag(s_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_page = page_create(s_idx, s_cont);
    lv_obj_set_pos(s_page, 0, 0);

    dots_build();

    /* Close handle. The hardware button must never be the only way out -
     * same reasoning as the on-screen menu handle on the pet screen. */
    lv_obj_t *x = lv_obj_create(s_scr);
    lv_obj_remove_style_all(x);
    lv_obj_set_size(x, 56, 40);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(x, 12, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(0x2A2A34), 0);
    lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(x, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *xl = lv_label_create(x);
    lv_label_set_text(xl, "Close");
    lv_obj_set_style_text_color(xl, lv_color_hex(0xD0D4DC), 0);
    lv_obj_center(xl);

    lv_obj_add_event_cb(s_scr, gesture_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(s_scr, gesture_cb, LV_EVENT_PRESSING,   NULL);
    lv_obj_add_event_cb(s_scr, gesture_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(s_scr, gesture_cb, LV_EVENT_PRESS_LOST, NULL);

    s_open = true;
    s_busy = false;
    s_pressed = s_swiped = false;

    /* Bubbles are suppressed while a page is open [SPEC section 10]. */
    ui_bubble_set_suppressed(true);

    lv_scr_load(s_scr);
    Serial.printf("MENU open  -> page %u %s\n", s_idx, page_name(s_idx));
}

void menu_close(void)
{
    if (!s_open) return;
    s_open = false;
    s_busy = false;

    ui_bubble_set_suppressed(false);
    scr_main_show();

    /* Delete AFTER the pet screen is loaded: deleting the active screen is
     * how you get a blank panel and a crash on the next refresh. */
    lv_obj_t *dead = s_scr;
    s_scr = s_cont = s_page = NULL;
    lv_obj_del(dead);

    Serial.println("MENU close -> pet screen");
}

void menu_toggle(void) { if (s_open) menu_close(); else menu_open(); }
