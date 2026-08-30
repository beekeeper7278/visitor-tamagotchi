/* farewell - end of visit. See farewell.h. */

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "forms.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "gamerec.h"
#include "evolve.h"
#include "journal.h"
#include "visitrec.h"
#include "persist.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "scr_main.h"
#include "menu.h"
#include "farewell.h"

static lv_obj_t *s_scr;
static bool      s_active;
static char      s_note[FAREWELL_MAX];

bool farewell_active(void) { return s_active; }

bool farewell_due(void)
{
    const pet_state_t *p = pet_get();
    return p->hatch_ts && p->days_alive >= VISIT_LENGTH_DAYS && !s_active;
}

/* --- the note ------------------------------------------------------------ */

void farewell_compose(char *out, size_t len)
{
    const pet_state_t *p = pet_get();
    const gamerec_t   *g = gamerec_get();

    /* What went WELL is chosen first and always leads, because the note has
     * to open warmly whatever kind of visit it was. */
    const char *praise;
    if (p->care_fed >= 70.0f)        praise = "You kept me so well fed";
    else if (p->care_clean >= 70.0f) praise = "You kept my room lovely and tidy";
    else if (g->total_games >= 5)    praise = "We played SO many games";
    else if (p->care_happy >= 60.0f) praise = "You always cheered me up";
    else                             praise = "You were there when it counted";

    /* One memorable thing, drawn from what actually happened. */
    char memory[80];
    if (p->cakes_eaten >= 3)
        snprintf(memory, sizeof(memory), "I'm still thinking about all that cake");
    else if (g->maze_best_ms && g->maze_best_ms < 30000)
        snprintf(memory, sizeof(memory), "that maze run was UNREAL");
    else if (p->accidents > 0)
        snprintf(memory, sizeof(memory), "we do not talk about the accident");
    else if (g->total_games > 0)
        snprintf(memory, sizeof(memory), "%s was my favourite", gamerec_name(gamerec_favorite()));
    else
        snprintf(memory, sizeof(memory), "the quiet days were nice too");

    /* Improvement ONLY when genuinely warranted, and phrased gently. */
    const char *improve = "";
    if (p->care_clean < 40.0f)      improve = " Maybe we could tidy up a bit faster next time...";
    else if (p->care_fed < 40.0f)   improve = " Maybe a few more snacks next time?";
    else if (p->lights_forgotten > 2) improve = " And the light! You kept leaving the light on!";

    /* Personality colours the sign-off, not the facts. */
    const char *sign;
    if (p->trait_a == PERS_MISCHIEVOUS || p->trait_b == PERS_MISCHIEVOUS)
        sign = "You tried to keep me out of trouble. You almost succeeded!";
    else if (p->trait_a == PERS_DRAMATIC || p->trait_b == PERS_DRAMATIC)
        sign = "This is the MOST emotional I have ever been. Goodbye!";
    else if (p->trait_a == PERS_SHY || p->trait_b == PERS_SHY)
        sign = "I'll miss you. A lot. Even if I didn't say so much.";
    else
        sign = "I'm going to miss you!";

    snprintf(out, len,
             "Earth was amazing! %s, and %s.%s %s",
             praise, memory, improve, sign);
}

/* --- the screen ---------------------------------------------------------- */

static void goodbye_cb(lv_event_t *e);

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_scr);
    lv_label_set_text(t, "Goodbye!");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFD24A), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14);

    const pet_state_t *p = pet_get();
    char sub[96];
    snprintf(sub, sizeof(sub), "%s  -  %u days on Earth",
             forms_name(p->form_id), p->days_alive);
    lv_obj_t *sl = lv_label_create(s_scr);
    lv_label_set_text(sl, sub);
    lv_obj_set_style_text_color(sl, lv_color_hex(0x8890A0), 0);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, 50);

    /* The note scrolls, because a good one will not always fit. */
    lv_obj_t *box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, BSP_LCD_W - 32, 250);
    lv_obj_set_pos(box, 16, 80);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x14141C), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);

    lv_obj_t *nl = lv_label_create(box);
    lv_label_set_text(nl, s_note);
    lv_label_set_long_mode(nl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(nl, BSP_LCD_W - 32 - 24);
    lv_obj_set_style_text_font(nl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(nl, lv_color_hex(0xE8E8E8), 0);

    lv_obj_t *b = lv_obj_create(s_scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BSP_LCD_W - 64, 74);
    lv_obj_set_pos(b, 32, 352);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x60D0A0), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, goodbye_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, "Say goodbye");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(0x101018), 0);
    lv_obj_center(bl);
}

void farewell_begin(void)
{
    if (s_active) return;
    s_active = true;

    ui_bubble_set_suppressed(true);
    ui_pet_set_wander(false);
    if (menu_is_open()) menu_close();

    farewell_compose(s_note, sizeof(s_note));
    Serial.println();
    Serial.println("=== FAREWELL ==============================================");
    Serial.printf("  %s\n", s_note);
    Serial.println("-----------------------------------------------------------");

    build_screen();
    lv_scr_load(s_scr);
}

/* Only AFTER the goodbye is acknowledged does anything get archived or
 * reset. Nothing is destroyed while the note is still on screen. */
static void goodbye_do(void)
{
    visitrec_archive(s_note);

    pet_init();                 /* fresh Visitor, new personality */
    journal_clear();
    care_reset();
    gamerec_reset();            /* records are per-Visitor; history is kept */

    /* The next Visitor arrives as an EGG, not a Baby: pet_init() has already
     * set stage EGG and rolled a shell colour. Hatching happens when the
     * player starts the timer. */
    pet_state_t *p = pet_mutable();
    p->hatch_ts = 0;
    if (rtc_trusted()) p->last_sim_ts = rtc_now();
    persist_save(true);

    s_active = false;
    lv_obj_t *dead = s_scr;
    s_scr = nullptr;
    scr_main_show();
    if (dead) lv_obj_del(dead);

    ui_bubble_set_suppressed(false);
    ui_pet_set_egg(true, p->egg_color);
    Serial.println("FAREWELL: archived - a new egg is waiting");
}

static void goodbye_cb(lv_event_t *e) { (void)e; goodbye_do(); }
void farewell_acknowledge(void) { if (s_active) goodbye_do(); }
