/* pages - the six menu pages. See pages.h. */

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "forms.h"
#include "pet.h"
#include "pages.h"
#include "care.h"
#include "menu.h"
#include "rtc.h"
#include "persist.h"
#include "setclock.h"

/* --- shared page furniture ---------------------------------------------- */

static lv_obj_t *page_root(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x141420), 0);
    lv_obj_set_style_bg_grad_color(p, lv_color_hex(0x0B0B12), 0);
    lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    /* Gestures are handled on the SCREEN, so page content must let events
     * bubble up - otherwise a swipe that begins on a full-width Food card
     * never reaches the pager and the page feels stuck. */
    lv_obj_add_flag(p, LV_OBJ_FLAG_EVENT_BUBBLE);
    return p;
}

static void title(lv_obj_t *p, const char *txt)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xE8E8E8), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 26);
}

static void note(lv_obj_t *p, const char *txt, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, BSP_LCD_W - 56);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x6C7484), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
}

/* A labelled horizontal bar. Bars not arcs: cheaper to redraw and far easier
 * for a five-year-old to read [SPEC section 4]. */
static void stat_bar(lv_obj_t *p, const char *name, float pct,
                     uint32_t colour, lv_coord_t y)
{
    const lv_coord_t W = BSP_LCD_W - 72, H = 22;

    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_color(l, lv_color_hex(0xB0B8C8), 0);
    lv_obj_set_pos(l, 36, y - 22);

    lv_obj_t *track = lv_obj_create(p);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, W, H);
    lv_obj_set_pos(track, 36, y);
    lv_obj_set_style_radius(track, H / 2, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x24242E), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    lv_coord_t fw = (lv_coord_t)(W * pct / 100.0f);
    if (fw < H && pct > 0.0f) fw = H;          /* keep the rounded cap legible */

    lv_obj_t *fill = lv_obj_create(p);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, fw, H);
    lv_obj_set_pos(fill, 36, y);
    lv_obj_set_style_radius(fill, H / 2, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *v = lv_label_create(p);
    lv_label_set_text_fmt(v, "%d", (int)(pct + 0.5f));
    lv_obj_set_style_text_color(v, lv_color_hex(0x8890A0), 0);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -36, y - 22);
}

/* A large touch target. 5-year-old fingers, so nothing small. */
static lv_obj_t *card(lv_obj_t *p, const char *label, lv_coord_t x, lv_coord_t y,
                      lv_coord_t w, lv_coord_t h, uint32_t colour, bool enabled)
{
    lv_obj_t *b = lv_obj_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 14, 0);
    /* Every card keeps its colour, wired or not. Greying out the unwired
     * ones made the Games and Care pages look broken rather than unfinished;
     * a full-colour card that simply does not respond yet reads much better
     * and keeps the two pages visually consistent with Food. The only cue is
     * a slightly softer fill and no highlight border. */
    lv_obj_set_style_bg_color(b, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(b, enabled ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(b, enabled ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    if (enabled) lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    else         lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x101018), 0);
    lv_obj_center(l);
    return b;
}

/* --- 1. Stats ------------------------------------------------------------ */

static void build_stats(lv_obj_t *p)
{
    const pet_state_t *s = pet_get();
    title(p, "Stats");

    stat_bar(p, "Hunger",      s->hunger,      0xE8A33D, 92);
    stat_bar(p, "Happiness",   s->happiness,   0x60D0A0, 154);
    stat_bar(p, "Discipline",  s->discipline,  0x7FA8E8, 216);
    stat_bar(p, "Cleanliness", s->cleanliness, 0x9E86E8, 278);

    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text_fmt(l, "%s   -   day %d   -   %d g\nfeeling %s",
                          pet_stage_name(s->stage), (int)s->days_alive,
                          (int)(s->weight_g + 0.5f), pet_mood_name(pet_mood()));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xB0B8C8), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 332);
}

/* --- 2. Food ------------------------------------------------------------- */

/* A swipe that starts on a card must not also activate the card. */
static void food_cb(lv_event_t *e)
{
    if (menu_swipe_active()) return;
    care_feed((food_t)(intptr_t)lv_event_get_user_data(e));
}

static void build_food(lv_obj_t *p)
{
    title(p, "Food");
    const lv_coord_t W = BSP_LCD_W - 72, H = 76;
    lv_obj_t *b;
    b = card(p, "Burger", 36, 96,  W, H, 0xE8A33D, true);
    lv_obj_add_event_cb(b, food_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FOOD_BURGER);
    b = card(p, "Fruit",  36, 192, W, H, 0x60D0A0, true);
    lv_obj_add_event_cb(b, food_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FOOD_FRUIT);
    b = card(p, "Cake",   36, 288, W, H, 0xFF9EB5, true);
    lv_obj_add_event_cb(b, food_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FOOD_CAKE);
}

/* --- 3. Games ------------------------------------------------------------ */

static void build_games(lv_obj_t *p)
{
    title(p, "Games");
    const lv_coord_t S = 140, GAP = 16;
    const lv_coord_t x0 = (BSP_LCD_W - (2 * S + GAP)) / 2, y0 = 96;
    card(p, "Reaction", x0,             y0,             S, S, 0xFF8A75, false);
    card(p, "Higher",   x0 + S + GAP,   y0,             S, S, 0xF2C14E, false);
    card(p, "Memory",   x0,             y0 + S + GAP,   S, S, 0x5FCBB4, false);
    card(p, "Maze",     x0 + S + GAP,   y0 + S + GAP,   S, S, 0xA894EE, false);
    note(p, "Games arrive in their own phase.", y0 + 2 * S + GAP + 14);
}

/* --- 4. Care ------------------------------------------------------------- */

static void care_bathroom_cb(lv_event_t *e) { (void)e; if (menu_swipe_active()) return; care_bathroom(); }
static void care_lights_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    care_set_lights(!care_lights_on());
    Serial.printf("LIGHTS %s\n", care_lights_on() ? "ON" : "OFF");
    persist_mark_dirty("lights");
    menu_rebuild_page();          /* so the label reflects the new state */
}

static void care_clean_cb(lv_event_t *e)    { (void)e; if (menu_swipe_active()) return; care_clean(); }

static void build_care(lv_obj_t *p)
{
    title(p, "Care");
    const lv_coord_t W = BSP_LCD_W - 72, H = 68;
    lv_obj_t *b;
    b = card(p, "Bathroom", 36, 96,  W, H, 0x7FA8E8, true);
    lv_obj_add_event_cb(b, care_bathroom_cb, LV_EVENT_CLICKED, NULL);
    b = card(p, "Clean Up", 36, 180, W, H, 0x9E86E8, true);
    lv_obj_add_event_cb(b, care_clean_cb, LV_EVENT_CLICKED, NULL);
    /* Lights is live from Milestone 5: OFF during scheduled sleep gives the
     * better energy recovery, ON gives reduced recovery and marks the
     * forgotten-lights history. The label states the CURRENT state, because
     * a light switch whose position you cannot read is useless. */
    char lb[24];
    snprintf(lb, sizeof(lb), "Lights: %s", care_lights_on() ? "ON" : "OFF");
    b = card(p, lb, 36, 264, W, H, care_lights_on() ? 0xF2C14E : 0x6B5A2E, true);
    lv_obj_add_event_cb(b, care_lights_cb, LV_EVENT_CLICKED, NULL);

    /* Discipline belongs to the stat model that owns it. */
    card(p, "Discipline", 36, 348, W, H, 0x8FCB9B, false);
}

/* --- 5. Clock / Pet Info ------------------------------------------------- */

static void setclock_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    setclock_open();
}

static void build_clock(lv_obj_t *p)
{
    const pet_state_t *s = pet_get();
    title(p, "Pet Info");

    char tb[40];
    if (rtc_trusted()) {
        rtc_time_t t;
        rtc_read(&t);
        const uint8_t h12 = (t.hour % 12) ? (t.hour % 12) : 12;
        snprintf(tb, sizeof(tb), "%d:%02d %s", h12, t.min, t.hour >= 12 ? "PM" : "AM");
    } else {
        snprintf(tb, sizeof(tb), "--:--");
    }
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, tb);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l,
        lv_color_hex(rtc_trusted() ? 0xE8E8E8 : 0x3A3A46), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 92);

    if (!rtc_trusted())
        note(p, "Clock not set. Aging and offline catch-up stay paused until "
                "a real date and time are entered.", 126);

    lv_obj_t *sb = card(p, "Set Date & Time", 36, 166, BSP_LCD_W - 72, 56,
                        0x7FA8E8, true);
    lv_obj_add_event_cb(sb, setclock_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *i = lv_label_create(p);
    lv_label_set_text_fmt(i, "stage    %s\nday      %d\nweight   %d g\nform     %s",
                          pet_stage_name(s->stage), (int)s->days_alive,
                          (int)(s->weight_g + 0.5f), forms_name(s->form_id));
    lv_obj_set_style_text_color(i, lv_color_hex(0xB0B8C8), 0);
    lv_obj_align(i, LV_ALIGN_TOP_MID, 0, 240);

    /* Volume control hook reserved for the audio phase - see
     * docs/PHASE10-AUDIO-REQUIREMENTS.md. Not built, only reserved. */
    card(p, "Volume", 36, 336, BSP_LCD_W - 72, 50, 0x9AA6C4, false);
    note(p, "Volume arrives with audio. Mute will silence sound only.", 392);
}

/* --- 6. Journal ---------------------------------------------------------- */

static void build_journal(lv_obj_t *p)
{
    title(p, "Journal");
    note(p, "The Journal scrolls VERTICALLY inside this page so it never "
            "fights the horizontal pager. Contents arrive with the journal "
            "phase: milestones, Visit Records, favourite food and game, and "
            "the farewell note.", 100);
}

/* --- dispatch ------------------------------------------------------------ */

lv_obj_t *page_create(uint8_t idx, lv_obj_t *parent)
{
    lv_obj_t *p = page_root(parent);
    switch (idx) {
        case PAGE_STATS:   build_stats(p);   break;
        case PAGE_FOOD:    build_food(p);    break;
        case PAGE_GAMES:   build_games(p);   break;
        case PAGE_CARE:    build_care(p);    break;
        case PAGE_CLOCK:   build_clock(p);   break;
        case PAGE_JOURNAL: build_journal(p); break;
        default: break;
    }
    return p;
}

const char *page_name(uint8_t idx)
{
    switch (idx) {
        case PAGE_STATS:   return "Stats";
        case PAGE_FOOD:    return "Food";
        case PAGE_GAMES:   return "Games";
        case PAGE_CARE:    return "Care";
        case PAGE_CLOCK:   return "Pet Info";
        case PAGE_JOURNAL: return "Journal";
        default:           return "?";
    }
}
