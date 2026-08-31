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
#include "motion.h"
#include "audio.h"
#include "settings.h"
#include "games.h"
#include "discipline.h"
#include "evolve.h"
#include "journal.h"
#include "visitrec.h"
#include "rtc.h"
#include "gamerec.h"
#include "dialogue.h"
#include "discipline.h"

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
    /* 1 real day = 1 Visitor year - see the Settings page for why the
     * child-facing copy says years and the console still says days. */
    lv_label_set_text_fmt(l, "%s   -   %d years old   -   %d g\nfeeling %s",
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

static void game_cb(lv_event_t *e)
{
    if (menu_swipe_active()) return;
    games_launch((uint8_t)(intptr_t)lv_event_get_user_data(e));
}

static void build_games(lv_obj_t *p)
{
    title(p, "Games");
    const lv_coord_t S = 140, GAP = 16;
    const lv_coord_t x0 = (BSP_LCD_W - (2 * S + GAP)) / 2, y0 = 96;
    lv_obj_t *b;
    b = card(p, "Reaction", x0,           y0,           S, S, 0xFF8A75, true);
    lv_obj_add_event_cb(b, game_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAME_REACT);
    b = card(p, "Higher",   x0 + S + GAP, y0,           S, S, 0xF2C14E, true);
    lv_obj_add_event_cb(b, game_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAME_HILO);
    b = card(p, "Memory",   x0,           y0 + S + GAP, S, S, 0x5FCBB4, true);
    lv_obj_add_event_cb(b, game_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAME_MEMORY);
    b = card(p, "Maze",     x0 + S + GAP, y0 + S + GAP, S, S, 0xA894EE, true);
    lv_obj_add_event_cb(b, game_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAME_MAZE);

    const gamerec_t *r = gamerec_get();
    char b2[80];
    snprintf(b2, sizeof(b2), "%u games played   favourite: %s",
             r->total_games, gamerec_name(gamerec_favorite()));
    note(p, b2, y0 + 2 * S + GAP + 14);
}

/* --- 4. Care ------------------------------------------------------------- */

static void care_bathroom_cb(lv_event_t *e) { (void)e; if (menu_swipe_active()) return; care_bathroom(); }
static void care_lights_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    /* The PLAYER pressing the switch, so the Visitor gets to react - and the
     * reaction is deferred until this page closes, because it is spoken on
     * the pet screen and would otherwise expire behind the menu. */
    care_player_toggle_lights(!care_lights_on());
    persist_mark_dirty("lights");
    menu_rebuild_page();          /* so the label reflects the new state */
}

static void care_disc_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    discipline_press();
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

    /* Live from Milestone 8. The label says whether there is actually
     * something to tell off, so the player is not guessing. */
    b = card(p, discipline_window_open() ? "Discipline!" : "Discipline",
             36, 348, W, H,
             discipline_window_open() ? 0xF2A05A : 0x8FCB9B, true);
    lv_obj_add_event_cb(b, care_disc_cb, LV_EVENT_CLICKED, NULL);
}

/* --- 5. Settings --------------------------------------------------------
 * WAS "Pet Info". Renamed in Phase 9.5 because that is what it had already
 * become: it owns the clock, and it is where the device-level controls
 * belong. Volume, Recalibrate Tilt and Gravity Reactions are RESERVED here
 * with visible, disabled cards - Phase 10 owns their behaviour. A reserved
 * card that is visibly not yet wired is honest; an empty page that grows
 * three controls later is a surprise.
 *
 * The Visitor's own information stays, because a parent looking for "how old
 * is it" looks here, not on the Journal.
 *
 * AGE IS SHOWN IN VISITOR YEARS. 1 real day = 1 Visitor year, which is
 * already how the stage boundaries are defined (Kid at 1, Teen at 3, Adult
 * at 6). "day 6" told a child nothing; "6 years old" is the same number
 * saying something. The DIAGNOSTICS keep printing day numbers - a developer
 * needs the raw figure and the console is not a child-facing surface. */

static void setclock_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    setclock_open();
}

static const char *volume_name(uint8_t v)
{
    switch (v) {
        case VOL_MUTE: return "Mute";
        case VOL_LOW:  return "Low";
        case VOL_MED:  return "Medium";
        default:       return "High";
    }
}

/* Tap cycles Mute -> Low -> Medium -> High -> Mute. The confirmation chirp
 * plays AFTER the new level is applied, so you hear what you just chose -
 * and at Mute you correctly hear nothing, which is the point. */
static void volume_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    const uint8_t next = (uint8_t)((settings_volume() + 1) % VOL_COUNT);
    settings_set_volume(next);
    audio_set_volume(next);
    audio_play(SND_UI_CONFIRM);
    menu_rebuild_page();          /* so the label reflects the new state */
}

static void gravity_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    const bool on = !settings_gravity_on();
    settings_set_gravity(on);
    /* Turning it back ON starts from calm: the saved calibration is kept,
     * but temporary motion annoyance is not something to inherit from
     * before the switch was flipped. */
    audio_play(on ? SND_UI_CONFIRM : SND_UI_REFUSE);
    menu_rebuild_page();          /* so the label reflects the new state */
}

static void calibrate_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    if (motion_calibrating()) return;
    /* Calibration works whether or not gravity reactions are on - it is a
     * device setting, and Tilt Maze wants it either way. */
    ui_bubble_say(BUBBLE_T1_REACTION, "Hold Visitor how you normally use it.");
    motion_calibrate_start();
    menu_rebuild_page();          /* so the label reflects the new state */
}

static void build_settings(lv_obj_t *p)
{
    const pet_state_t *s = pet_get();
    title(p, "Settings");

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
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 78);

    lv_obj_t *sb = card(p, "Set Date & Time", 36, 124, BSP_LCD_W - 72, 52,
                        0x7FA8E8, true);
    lv_obj_add_event_cb(sb, setclock_cb, LV_EVENT_CLICKED, NULL);

    if (!rtc_trusted()) {
        /* Without a trusted clock the age is not merely unknown, it does not
         * exist - pet_age_days() returns 0 by design. Printing "0 years old"
         * would be a lie dressed as data, so the explanation takes the slot
         * the information would have used. */
        note(p, "Clock not set. Aging and offline catch-up stay paused until "
                "a real date and time are entered.", 190);
    } else {
        lv_obj_t *i = lv_label_create(p);
        lv_label_set_text_fmt(i,
            "age      %u years old\nstage    %s\nlooks    %s\nweight   %d g\n%s",
            (unsigned)s->days_alive, pet_stage_name(s->stage),
            forms_long_name(s->form_id), (int)(s->weight_g + 0.5f),
            s->stage == STAGE_EGG ? "not hatched yet"
                                  : (s->gender == GENDER_GIRL ? "a girl" : "a boy"));
        lv_obj_set_style_text_color(i, lv_color_hex(0xB0B8C8), 0);
        lv_obj_align(i, LV_ALIGN_TOP_MID, 0, 190);
    }

    /* --- PHASE 10: the three reserved cards, now live -------------------
     * Each shows its CURRENT value in the label rather than opening a
     * sub-screen. A parent checking "is it muted?" gets the answer by
     * looking, and a tap cycles - which is the whole interaction a
     * four-option setting needs on a 368 px panel. */
    char vb[32];
    snprintf(vb, sizeof(vb), "Volume:  %s", volume_name(settings_volume()));
    lv_obj_t *vc = card(p, vb, 36, 288, BSP_LCD_W - 72, 46, 0x9AA6C4, true);
    lv_obj_add_event_cb(vc, volume_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cc = card(p, motion_calibrating() ? "Hold still..." : "Recalibrate Tilt",
                        36, 340, BSP_LCD_W - 72, 46, 0x9AA6C4, true);
    lv_obj_add_event_cb(cc, calibrate_cb, LV_EVENT_CLICKED, NULL);

    char gb[32];
    snprintf(gb, sizeof(gb), "Gravity:  %s", settings_gravity_on() ? "ON" : "OFF");
    lv_obj_t *gc = card(p, gb, 36, 392, BSP_LCD_W - 72, 46, 0x9AA6C4, true);
    lv_obj_add_event_cb(gc, gravity_cb, LV_EVENT_CLICKED, NULL);
}

/* --- 6. Journal ---------------------------------------------------------- */

/* A titled card. Sections rather than one wall of text - the Journal is a
 * keepsake a child browses, not a debug dump. */
static lv_obj_t *jcard(lv_obj_t *par, const char *title_txt, lv_coord_t *y,
                       uint32_t accent)
{
    lv_obj_t *c = lv_obj_create(par);
    lv_obj_remove_style_all(c);
    lv_obj_set_width(c, BSP_LCD_W - 40);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_pos(c, 20, *y);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x14141C), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title_txt);
    lv_obj_set_style_text_color(t, lv_color_hex(accent), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    return c;
}

static void jbody(lv_obj_t *card, const char *txt)
{
    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, BSP_LCD_W - 40 - 24);
    lv_obj_set_style_text_color(l, lv_color_hex(0xD0D4DC), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 22);
}

static void build_journal(lv_obj_t *p)
{
    const pet_state_t *st = pet_get();
    const gamerec_t *g = gamerec_get();
    char b[320];

    title(p, "Journal");

    /* THE SCROLLER. Vertical only: the pager owns horizontal, and giving
     * this LV_DIR_VER means LVGL will not fight it for a sideways drag. */
    lv_obj_t *sc = lv_obj_create(p);
    lv_obj_remove_style_all(sc);
    lv_obj_set_size(sc, BSP_LCD_W, BSP_LCD_H - 74);
    lv_obj_set_pos(sc, 0, 70);
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    /* Events still bubble so a clearly-horizontal swipe reaches the pager. */
    lv_obj_add_flag(sc, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_coord_t y = 0;
    lv_obj_t *c;

    /* --- who ---
     * The GENDER line sits at the top of the first card on purpose. The
     * "It's a boy!" reveal happens once, at the hatch, and a child can
     * easily miss it - so the Journal carries the answer somewhere
     * unmissable rather than nowhere. */
    c = jcard(sc, "This Visitor", &y, 0x60D0A0);
    char when[32] = "not recorded";
    if (st->hatch_ts) rtc_format(st->hatch_ts, when, sizeof(when));
    snprintf(b, sizeof(b),
             "It's a %s!\n%s  (%s)\n%u years old\nArrived %.10s\nA bit %s, a bit %s",
             st->gender == GENDER_GIRL ? "girl" : "boy",
             forms_long_name(st->form_id), pet_stage_name(st->stage),
             st->days_alive,
             when, evolve_trait_name(st->trait_a), evolve_trait_name(st->trait_b));
    jbody(c, b);
    lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;

    /* --- About Me, in the Visitor's own voice --- */
    c = jcard(sc, "About Me", &y, 0xFFC46B);
    dialogue_about_me(b, sizeof(b));
    jbody(c, b);
    lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;

    /* --- favourites --- */
    c = jcard(sc, "Favourites", &y, 0xF2C14E);
    snprintf(b, sizeof(b), "Food: %s\nGame: %s",
             evolve_food_name(evolve_favourite_food()),
             g->total_games ? gamerec_name(gamerec_favorite()) : "still deciding");
    jbody(c, b);
    lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;

    /* --- how it grew --------------------------------------------------
     * THE REAL, PERSISTED PATH - one rung per stage actually lived, in
     * order, with the actual form names.
     *
     * This used to print a hardcoded "Baby" and then whatever evo_path[]
     * happened to contain, which for most Visitors was nothing: the only
     * writer ran in the offline catch-up and its index expression excluded
     * the Adult slot entirely. So a Visitor raised on a device that stayed
     * switched on showed "Baby" and nothing else, however far it had grown.
     * pet_record_form() now writes from every path, and a save from before
     * that is backfilled at load with what is knowable.
     *
     * Rendered vertically with arrows because it is a ladder, not a
     * sentence, and because four adult names on one line wrapped badly.
     * Each stage appears EXACTLY once - the slot IS the stage. */
    c = jcard(sc, "How I grew up", &y, 0xA894EE);
    {
        static const char *STAGE_LABEL[4] = { "Baby", "Kid", "Teen", "Adult" };
        /* How many rungs this Visitor has actually reached. */
        const uint8_t reached = (st->stage >= STAGE_BABY)
                              ? (uint8_t)(st->stage - STAGE_BABY + 1) : 0;
        int n = 0; b[0] = 0;
        for (uint8_t i = 0; i < reached && i < 4; i++) {
            if (i) n += snprintf(b + n, sizeof(b) - n, "\n  |\n  v\n");
            /* Index 0 is ALWAYS the Baby - see pet_backfill_evo_path() for
             * why a zero there is not a missing value. */
            if (i == 0 || st->evo_path[i])
                n += snprintf(b + n, sizeof(b) - n, "%s",
                              forms_long_name(i == 0 ? FORM_BABY : st->evo_path[i]));
            else
                /* Honest rather than invented. Re-deriving a form from
                 * today's accumulators would write a guess into the past. */
                n += snprintf(b + n, sizeof(b) - n, "%s (not recorded)", STAGE_LABEL[i]);
        }
        if (!reached) n += snprintf(b + n, sizeof(b) - n, "Still an egg!");
        else if (st->stage <= STAGE_BABY)
            snprintf(b + n, sizeof(b) - n, "\n(still little!)");
    }
    jbody(c, b);
    lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;

    /* --- dreams, newest first --- */
    if (st->dream_n) {
        c = jcard(sc, "My dreams", &y, 0x8FD3E8);
        int n = 0; b[0] = 0;
        for (int8_t i = (int8_t)st->dream_n - 1; i >= 0; i--) {
            n += snprintf(b + n, sizeof(b) - n, "%s%s",
                          n ? "\n\n" : "",
                          dialogue_dream_journal(st->dream_id[i]));
            if (n >= (int)sizeof(b) - 120) break;
        }
        jbody(c, b);
        lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;
    }

    /* --- games --- */
    c = jcard(sc, "Games", &y, 0xFF8A75);
    int n = snprintf(b, sizeof(b), "%u games played\n", g->total_games);
    for (uint8_t i = 0; i < GAME_COUNT; i++)
        if (g->plays[i])
            n += snprintf(b + n, sizeof(b) - n, "%s: best %u\n",
                          gamerec_name(i), g->best[i]);
    if (g->maze_best_ms)
        snprintf(b + n, sizeof(b) - n, "Maze best %lu.%02lus",
                 (unsigned long)(g->maze_best_ms / 1000),
                 (unsigned long)((g->maze_best_ms % 1000) / 10));
    jbody(c, b);
    lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;

    /* --- the funny bits, only when there is something to say --- */
    n = 0; b[0] = 0;
    if (st->cakes_eaten)      n += snprintf(b + n, sizeof(b) - n, "Cakes eaten: %u\n", st->cakes_eaten);
    if (st->accidents)        n += snprintf(b + n, sizeof(b) - n, "Little accidents: %u\n", st->accidents);
    if (st->lights_forgotten) n += snprintf(b + n, sizeof(b) - n, "Nights with the light left on: %u\n", st->lights_forgotten);
    if (st->disc_opportunities) n += snprintf(b + n, sizeof(b) - n, "Mischief: %u (told off %u)\n", st->disc_opportunities, st->disc_correct);
    if (st->disc_opportunities >= 3)
        n += snprintf(b + n, sizeof(b) - n, "Behaviour now: %s\n",
                      discipline_learned() >= 65.0f ? "quite the handful"
                    : discipline_learned() <= 35.0f ? "very well behaved"
                                                    : "mostly good");
    if (n) {
        c = jcard(sc, "Notable", &y, 0x7FA8E8);
        jbody(c, b);
        lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;
    }

    /* --- milestones, newest first --- */
    if (journal_count()) {
        c = jcard(sc, "Milestones", &y, 0xFFB6C8);
        n = 0; b[0] = 0;
        for (uint8_t i = 0; i < journal_count() && i < 8; i++) {
            char line[96];
            if (!journal_line(i, line, sizeof(line))) break;
            n += snprintf(b + n, sizeof(b) - n, "%s\n", line);
            if (n >= (int)sizeof(b) - 96) break;
        }
        jbody(c, b);
        lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;
    }

    /* --- who came before --- */
    if (visitrec_count()) {
        c = jcard(sc, "Visitors before", &y, 0x9AA6C4);
        n = 0; b[0] = 0;
        for (uint8_t i = 0; i < visitrec_count() && i < 4; i++) {
            const visit_rec_t *r = visitrec_at(i);
            if (!r) continue;
            /* "stayed N days" is a DURATION and says so, which is what a
             * parent wants from this card. The Visitor's own age is on the
             * cards above, in Visitor years. */
            n += snprintf(b + n, sizeof(b) - n, "%s - stayed %u days, loved %s\n",
                          forms_long_name(r->final_form), r->days,
                          evolve_food_name(r->fav_food));
        }
        jbody(c, b);
        lv_obj_update_layout(c); y += lv_obj_get_height(c) + 10;
    }
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
        case PAGE_CLOCK:   build_settings(p); break;
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
        case PAGE_CLOCK:   return "Settings";
        case PAGE_JOURNAL: return "Journal";
        default:           return "?";
    }
}
