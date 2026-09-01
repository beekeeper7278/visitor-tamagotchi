/* setclock - touch date and time setter. See setclock.h. */

#include <Arduino.h>
#include <stdio.h>
#include <lvgl.h>
#include <string.h>

#include "board_pins.h"
#include "config.h"
#include "rtc.h"
#include "pet.h"
#include "persist.h"
#include "sim.h"
#include "settings.h"
#include "setclock.h"

static lv_obj_t *s_scr, *s_prev;
static lv_obj_t *s_lbl_date, *s_lbl_time, *s_lbl_err;
static bool s_open;

/* Working values, seeded in setclock_open() - from the RTC when it is
 * already sensible (so CORRECTING is an adjustment) and from the firmware
 * build stamp when it is not. There is deliberately no hardcoded date here:
 * see rtc_build_stamp(). */
static int s_y, s_mo, s_d, s_h12, s_mi, s_pm;

bool setclock_is_open(void) { return s_open; }

static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

static int days_in_month(int y, int m)
{
    static const int d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && is_leap(y)) return 29;
    return d[(m - 1) % 12];
}

static lv_obj_t *s_val[3];
static int       s_step;

static void refresh(void)
{
    static const char *MON[] = { "Jan","Feb","Mar","Apr","May","Jun",
                                 "Jul","Aug","Sep","Oct","Nov","Dec" };
    char b[64];
    snprintf(b, sizeof(b), "%s %d, %d", MON[s_mo - 1], s_d, s_y);
    lv_label_set_text(s_lbl_date, b);
    snprintf(b, sizeof(b), "%d:%02d %s", s_h12, s_mi, s_pm ? "PM" : "AM");
    lv_label_set_text(s_lbl_time, b);
    lv_label_set_text(s_lbl_err, "");

    /* per-row values */
    if (s_step == 0) {
        if (s_val[0]) { snprintf(b, sizeof(b), "%s", MON[s_mo - 1]); lv_label_set_text(s_val[0], b); }
        if (s_val[1]) { snprintf(b, sizeof(b), "%d", s_d);  lv_label_set_text(s_val[1], b); }
        if (s_val[2]) { snprintf(b, sizeof(b), "%d", s_y);  lv_label_set_text(s_val[2], b); }
    } else {
        if (s_val[0]) { snprintf(b, sizeof(b), "%d", s_h12); lv_label_set_text(s_val[0], b); }
        if (s_val[1]) { snprintf(b, sizeof(b), "%02d", s_mi); lv_label_set_text(s_val[1], b); }
        if (s_val[2]) lv_label_set_text(s_val[2], s_pm ? "PM" : "AM");
    }
}

/* Clamp the day whenever month or year changes, so 31 Jan -> Feb becomes the
 * 28th or 29th instead of an impossible date reaching the RTC. */
static void clamp_day(void)
{
    const int max = days_in_month(s_y, s_mo);
    if (s_d > max) s_d = max;
    if (s_d < 1)   s_d = 1;
}

typedef struct { int *v; int lo, hi; bool wrap; } field_t;
static field_t F_MO = { &s_mo, 1, 12, true };
static field_t F_D  = { &s_d,  1, 31, true };
static field_t F_Y  = { &s_y,  2024, 2054, false };
static field_t F_H  = { &s_h12, 1, 12, true };
static field_t F_MI = { &s_mi, 0, 59, true };

static void step_cb(lv_event_t *e)
{
    field_t *f = (field_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    const int delta = (int)(intptr_t)lv_obj_get_user_data(btn);

    *f->v += delta;
    if (*f->v > f->hi) *f->v = f->wrap ? f->lo : f->hi;
    if (*f->v < f->lo) *f->v = f->wrap ? f->hi : f->lo;
    clamp_day();
    refresh();
}

static void ampm_cb(lv_event_t *e)
{
    (void)e;
    s_pm = !s_pm;
    refresh();
}

static void close_modal(void)
{
    s_open = false;
    lv_obj_t *dead = s_scr;
    s_scr = nullptr;
    if (s_prev) lv_scr_load(s_prev);
    if (dead) lv_obj_del(dead);
}

static void cancel_cb(lv_event_t *e) { (void)e; close_modal(); }

/* --- CONFIRM: SET, VERIFY, THEN REBASE ----------------------------------
 * Three things have to happen, in this order, and the order is the point.
 *
 *   1. READ THE OLD VALUE FIRST. A correction is the difference between two
 *      readings of the SAME clock, and after rtc_set() the old one is gone
 *      for good. Capturing it afterwards is not possible; capturing it late
 *      is not possible either. It is the first statement for that reason.
 *
 *   2. WRITE AND VERIFY. rtc_set() writes, clears the OS flag and confirms
 *      the clear stuck. That proves the clock is now TRUSTED; it does not
 *      prove it holds the value that was asked for. So the value is read
 *      back and compared field by field before anything downstream is told
 *      the date is now correct - "successfully set and verified" has to mean
 *      both halves, or the confirmation flag is just optimism.
 *
 *   3. REBASE, NEVER SIMULATE. This is a CLOCK CORRECTION: the clock was
 *      wrong and is now right, and no time has passed. sim_clock_corrected()
 *      moves every RTC-anchored timestamp by the same delta so that age, the
 *      hatch countdown, a held departure and the game-streak window all come
 *      out unchanged - and sets last_sim_ts to the new reading so the next
 *      boot cannot replay the correction as an absence.
 *
 * What used to be here instead was `if (!p->hatch_ts) { p->hatch_ts = now; }`
 * and nothing else. It got BOTH cases wrong. For a live Visitor it moved
 * last_sim_ts but left hatch_ts on the old clock, so a +5 day correction
 * made a three-hour-old Visitor five days old, evolving and packing to
 * leave. For an EGG it did something worse: it handed an unhatched Visitor
 * an age baseline and then called pet_apply_stage_for_day(0), which - since
 * STAGE_EGG is 0 and day 0 means Baby - promoted the egg straight to a Baby,
 * skipping the countdown, the colour and gender resolution, the reveal, the
 * first words and the hatch chime. Setting the clock hatched the egg. */
static void confirm_cb(lv_event_t *e)
{
    (void)e;
    clamp_day();

    int h24 = s_h12 % 12;
    if (s_pm) h24 += 12;

    rtc_time_t t;
    t.year = (uint16_t)s_y; t.month = (uint8_t)s_mo; t.day = (uint8_t)s_d;
    t.hour = (uint8_t)h24;  t.min = (uint8_t)s_mi;   t.sec = 0;

    /* 1. The BEFORE reading, while it still exists. 0 means there was no
     *    usable one - an unset or implausible clock - and therefore no delta
     *    to rebase by, which is a different case and handled as one below. */
    const uint32_t before = rtc_trusted() ? rtc_now() : 0;

    /* 2a. Write, clear OS, read back, verify OS still clear. */
    if (!rtc_set(&t)) {
        lv_label_set_text(s_lbl_err, "Could not set the clock - try again");
        return;
    }

    /* 2b. And verify the VALUE, not just the flag. Seconds are excluded on
     *     purpose: we write 0 and the clock is already counting by the time
     *     it is read back. Everything coarser than that must match exactly. */
    rtc_time_t rb;
    const uint32_t after = rtc_now();
    if (!after || !rtc_read(&rb) ||
        rb.year != t.year || rb.month != t.month || rb.day != t.day ||
        rb.hour != t.hour || rb.min != t.min) {
        Serial.println("RTC set: read-back did NOT match what was written - "
                       "the clock stays unconfirmed");
        settings_set_clock_confirmed(0);
        lv_label_set_text(s_lbl_err, "The clock did not keep that - try again");
        return;
    }

    /* 3. Rebase, or anchor. Neither simulates anything. */
    if (before) sim_clock_corrected(before, after);
    else        sim_clock_first_trusted(after);

    /* Only NOW is the clock confirmed: written by a human, read back, and
     * matching. This is what makes START available on the pre-hatch screen. */
    settings_set_clock_confirmed(after);

    /* FORCE the save rather than marking dirty. The rebase has just rewritten
     * every anchor in the pet state; if the power went before the next
     * periodic write, the RTC would come back holding the corrected time and
     * the save would come back holding the uncorrected anchors - which is the
     * original bug, reconstructed on the next boot by sim_catch_up(). The
     * corrected clock and the anchors rebased around it have to reach flash
     * together. */
    persist_mark_dirty("clock set");
    persist_save(true);
    close_modal();
}

/* --- building blocks -----------------------------------------------------
 * TWO STEPS - Date, then Time - rather than six controls crammed onto one
 * 368x448 panel. Cramming was the original mistake: 54x44 buttons with 48 px
 * row pitch are hard to hit and easy to mis-hit, and this is a device a
 * parent operates once, quickly, probably one-handed.
 *
 * Every control is a 60 px tall card, and the WHOLE card is the touch
 * target - not just the glyph inside it. The -/+ cards are 76x60 and the
 * rows are 76 px apart, so adjacent targets are never adjacent pixels. */

#define ROW_H        60
#define ROW_PITCH    76
#define SIDE_W       76
#define ROW_Y0       118

static lv_obj_t *s_content;

static lv_obj_t *big_btn(lv_obj_t *par, const char *txt, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h, uint32_t col, uint32_t fg,
                         const lv_font_t *font)
{
    lv_obj_t *b = lv_obj_create(par);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* The card itself is clickable, so the touch target is the whole card. */
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    return b;
}

/* One row: [ - ] [ label + value ] [ + ] */
static void row(lv_obj_t *par, int slot, const char *name, field_t *f)
{
    const lv_coord_t y = ROW_Y0 + slot * ROW_PITCH;

    lv_obj_t *m = big_btn(par, "-", 16, y, SIDE_W, ROW_H,
                          0x2A2A34, 0xE8E8E8, &lv_font_montserrat_28);
    lv_obj_set_user_data(m, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(m, step_cb, LV_EVENT_CLICKED, f);

    lv_obj_t *c = lv_obj_create(par);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 168, ROW_H);
    lv_obj_set_pos(c, 100, y);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x16161E), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nl = lv_label_create(c);
    lv_label_set_text(nl, name);
    lv_obj_set_style_text_color(nl, lv_color_hex(0x8890A0), 0);
    lv_obj_align(nl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *vl = lv_label_create(c);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(vl, lv_color_hex(0xE8E8E8), 0);
    lv_obj_align(vl, LV_ALIGN_BOTTOM_MID, 0, -2);
    s_val[slot] = vl;

    lv_obj_t *p2 = big_btn(par, "+", 276, y, SIDE_W, ROW_H,
                           0x2A2A34, 0xE8E8E8, &lv_font_montserrat_28);
    lv_obj_set_user_data(p2, (void *)(intptr_t)1);
    lv_obj_add_event_cb(p2, step_cb, LV_EVENT_CLICKED, f);
}

static void build_step(void);
static void next_cb(lv_event_t *e) { (void)e; s_step = 1; build_step(); }
static void back_cb(lv_event_t *e) { (void)e; s_step = 0; build_step(); }

static void build_step(void)
{
    lv_obj_clean(s_content);
    for (int i = 0; i < 3; i++) s_val[i] = nullptr;

    lv_obj_t *st = lv_label_create(s_content);
    lv_label_set_text(st, s_step == 0 ? "Step 1 of 2  -  Date"
                                      : "Step 2 of 2  -  Time");
    lv_obj_set_style_text_color(st, lv_color_hex(0x8890A0), 0);
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 90);

    if (s_step == 0) {
        row(s_content, 0, "Month", &F_MO);
        row(s_content, 1, "Day",   &F_D);
        row(s_content, 2, "Year",  &F_Y);

        lv_obj_t *cx = big_btn(s_content, "Cancel", 16, 358, 160, 64,
                               0x2A2A34, 0xB0B8C8, &lv_font_montserrat_20);
        lv_obj_add_event_cb(cx, cancel_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *nx = big_btn(s_content, "Next", 192, 358, 160, 64,
                               0x7FA8E8, 0x101018, &lv_font_montserrat_28);
        lv_obj_add_event_cb(nx, next_cb, LV_EVENT_CLICKED, NULL);
    } else {
        row(s_content, 0, "Hour",   &F_H);
        row(s_content, 1, "Minute", &F_MI);

        /* AM/PM gets a full-width card of its own - the same 60 px height as
         * every other control, rather than a cramped toggle. */
        lv_obj_t *ap = big_btn(s_content, "", 16, ROW_Y0 + 2 * ROW_PITCH,
                               336, ROW_H, 0x7FA8E8, 0x101018,
                               &lv_font_montserrat_28);
        lv_obj_add_event_cb(ap, ampm_cb, LV_EVENT_CLICKED, NULL);
        s_val[2] = lv_obj_get_child(ap, 0);

        lv_obj_t *bk = big_btn(s_content, "Back", 16, 358, 160, 64,
                               0x2A2A34, 0xB0B8C8, &lv_font_montserrat_20);
        lv_obj_add_event_cb(bk, back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *ok = big_btn(s_content, "Confirm", 192, 358, 160, 64,
                               0x60D0A0, 0x101018, &lv_font_montserrat_28);
        lv_obj_add_event_cb(ok, confirm_cb, LV_EVENT_CLICKED, NULL);
    }
    refresh();
}

void setclock_open(void)
{
    if (s_open) return;
    s_prev = lv_scr_act();
    s_step = 0;

    /* Seed from the RTC when it is already sensible, so CORRECTING the clock
     * is an adjustment rather than starting from scratch - and from the
     * firmware build stamp when it is not, so a first-time setup starts
     * within days of the truth instead of at a fixed development date. See
     * the note on the build_*() helpers. */
    rtc_time_t cur;
    int h24;
    if (rtc_trusted() && rtc_read(&cur)) {
        s_y = cur.year; s_mo = cur.month; s_d = cur.day;
        s_mi = cur.min; h24 = cur.hour;
    } else {
        rtc_time_t bs;
        rtc_build_stamp(&bs);
        s_y = bs.year; s_mo = bs.month; s_d = bs.day;
        s_mi = bs.min; h24 = bs.hour;
        Serial.printf("SETCLOCK: no trusted clock - starting from the firmware "
                      "build stamp (%04u-%02u-%02u %02u:%02u), never from a "
                      "hardcoded date\n",
                      (unsigned)bs.year, (unsigned)bs.month, (unsigned)bs.day,
                      (unsigned)bs.hour, (unsigned)bs.min);
    }
    if (s_y < 2024) s_y = 2024;          /* the year field's own floor */
    if (s_y > 2054) s_y = 2054;
    s_pm  = h24 >= 12;
    s_h12 = h24 % 12; if (s_h12 == 0) s_h12 = 12;

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0B0B12), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_scr);
    lv_label_set_text(t, "Set Date & Time");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xE8E8E8), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

    /* Live summary of BOTH halves, so the value being edited is always
     * readable at a glance and in a large font. */
    s_lbl_date = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_lbl_date, lv_color_hex(0x60D0A0), 0);
    lv_obj_align(s_lbl_date, LV_ALIGN_TOP_MID, 0, 46);

    s_lbl_time = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0xB0B8C8), 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, 74);

    s_lbl_err = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_lbl_err, lv_color_hex(0xD1656B), 0);
    lv_obj_align(s_lbl_err, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(s_lbl_err, "");

    s_content = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    clamp_day();
    build_step();
    s_open = true;
    lv_scr_load(s_scr);
}
