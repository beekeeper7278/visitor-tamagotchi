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
#include "rtc.h"
#include "evolve.h"
#include "journal.h"
#include "persist.h"
#include "visitrec.h"
#include "ui_bubble.h"
#include "discipline.h"
#include "dialogue.h"

/* THE SELECTOR SPACING, PROVEN AT BUILD TIME.
 *
 * The last bug on this screen was a bottom-row swatch whose touch box
 * OVERLAPPED START by 4 px, which looked completely fine. Numbers that are
 * only checked by eye get broken by the next layout change, so the two gaps
 * the specification names are asserted here instead. Both must stay at or
 * above 20 px, and the whole picker must fit the panel. */
static_assert(EGG_GENDER_GAP_ABOVE >= 20,
              "gender row must clear the colour rows by at least 20 px");
static_assert(EGG_START_DEAD_BAND >= 20,
              "START must be at least 20 px clear of every selector hitbox");
static_assert(EGG_START_Y + EGG_START_H <= BSP_LCD_H,
              "START runs off the bottom of the panel");
static_assert(4 * EGG_SW_W + 3 * EGG_SW_GAP_X <= BSP_LCD_W,
              "the colour row is wider than the panel");
static_assert(3 * EGG_GENDER_W + 2 * EGG_GENDER_GAP_X <= BSP_LCD_W,
              "the gender row is wider than the panel");

static lv_obj_t *s_scr;
static lv_obj_t *s_room_layer;
static lv_obj_t *s_pet_layer;
static lv_obj_t *s_pet_overlay;
static lv_obj_t *s_hud_mood;
static lv_obj_t *s_btn_menu;
static lv_obj_t *s_egg_btn, *s_egg_lbl;
static lv_obj_t *s_dim, *s_zlayer;
static lv_obj_t *s_swatch[EGG_PALETTE_COUNT + 1];
static lv_obj_t *s_gender_btn[3];
static lv_obj_t *s_reveal, *s_reveal_lbl;   /* the "It's a boy/girl!" banner */
static uint32_t  s_last_z_ms;

/* The hatch sequence, in two beats that must NOT overlap: the identity
 * reveal owns the screen first, then the Visitor's first words. Both are
 * driven from scr_main_egg_refresh(), which runs once a second - fine for
 * two events measured in seconds. Zero means "not pending". */
static uint32_t  s_reveal_until_ms;
static uint32_t  s_greet_at_ms;

/* Tapping the pet is a reaction; tapping the background is not. Routing both
 * through the pet's own object rather than the screen is what keeps that
 * distinction honest once the Phase 3 menu handle is added below. */
static void show_obj(lv_obj_t *o, bool v)
{
    if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void menu_btn_cb(lv_event_t *e) { (void)e; menu_open(); }

/* Starting the timer is the player's choice, so the arrival is an event they
 * kick off rather than something that happens while they are not looking. */
static const uint32_t SWATCH[EGG_PALETTE_COUNT] = {
    0xE04A4A, 0x9B6BD8, 0x5A9BE8, 0x5FBF6B, 0x4FC3B0, 0xF2C14E
};

static void egg_pick_cb(lv_event_t *e)
{
    pet_state_t *p = pet_mutable();
    if (p->stage != STAGE_EGG || p->egg_hatch_ts) return;   /* locked once started */
    p->egg_choice = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    /* A REAL colour is applied immediately so the shell shows it. Surprise is
     * deliberately NOT applied: egg_color still holds whatever was rolled at
     * pet_init(), and showing it would give away the answer before START. The
     * display palette comes from egg_choice - see egg_display_palette(). */
    if (p->egg_choice < EGG_PALETTE_COUNT) p->egg_color = p->egg_choice;
    Serial.printf("EGG: colour choice %s\n",
                  p->egg_choice >= EGG_PALETTE_COUNT ? "SURPRISE (hidden until hatch)"
                                                     : "picked");
    persist_mark_dirty("egg colour");
}

static void gender_pick_cb(lv_event_t *e)
{
    pet_state_t *p = pet_mutable();
    if (p->stage != STAGE_EGG || p->egg_hatch_ts) return;
    p->gender_choice = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    Serial.printf("EGG: gender choice %s\n",
                  p->gender_choice == GENDER_BOY  ? "Boy" :
                  p->gender_choice == GENDER_GIRL ? "Girl" :
                                                    "SURPRISE (hidden until hatch)");
    persist_mark_dirty("gender choice");
}

/* What the SHELL should be drawn as. Never simply egg_color: while the
 * player has chosen Surprise the resolved colour is a secret, and the
 * rainbow shell is what keeps it one. */
static uint8_t egg_display_palette(const pet_state_t *p)
{
    return (p->egg_choice >= EGG_PALETTE_COUNT) ? EGG_PAL_RAINBOW : p->egg_color;
}

void scr_main_egg_start(uint32_t secs)
{
    pet_state_t *p = pet_mutable();
    if (p->stage != STAGE_EGG || p->egg_hatch_ts) return;

    /* BOTH surprises resolve ONCE, here, and are persisted immediately - so a
     * power cut during the five-minute hatch brings back the same egg and the
     * same Visitor rather than rerolling either. Nothing downstream ever
     * re-rolls them: from this point egg_color and gender are history. */
    if (p->egg_choice >= EGG_PALETTE_COUNT) {
        p->egg_color = (uint8_t)random(0, EGG_PALETTE_COUNT);
        Serial.printf("EGG: Surprise colour resolved to palette %u (locked, hidden "
                      "until hatch)\n", p->egg_color);
    }
    if (p->gender_choice == GENDER_SURPRISE) {
        p->gender = (uint8_t)random(0, 2);
        Serial.printf("EGG: Surprise gender resolved to %s (locked, revealed at hatch)\n",
                      p->gender == GENDER_BOY ? "boy" : "girl");
    } else {
        p->gender = p->gender_choice;
    }

    const uint32_t now = rtc_trusted() ? rtc_now() : (millis() / 1000);
    p->egg_hatch_ts = now + secs;
    /* force: the resolved identity must be on flash BEFORE the timer is
     * allowed to run, not at the next periodic save. */
    persist_save(true);
    Serial.printf("EGG: hatching in %lu s\n", (unsigned long)secs);
}

static void egg_start_cb(lv_event_t *e)
{
    (void)e;
    if (menu_swipe_active()) return;
    scr_main_egg_start(EGG_HATCH_SEC);
}

/* --- the hatch, then the reveal, then the first words --------------------
 * Three beats, strictly in order and never overlapping:
 *   1. the shell opens and the Baby appears;
 *   2. IF the gender was a Surprise, a banner says which - on its own,
 *      because a reveal competing with a speech bubble is a reveal nobody
 *      reads;
 *   3. the Visitor says its first ever words.
 * Beat 2 is skipped when the player already knows, and beat 3 always runs. */
static void egg_hatch(void)
{
    pet_state_t *p = pet_mutable();
    const bool was_surprise = (p->gender_choice == GENDER_SURPRISE);

    p->stage        = STAGE_BABY;
    p->form_id      = FORM_BABY;
    p->egg_hatch_ts = 0;
    p->days_alive   = 0;
    p->stage_start_day = 0;

    /* Belt and braces: if this egg somehow reached the hatch without going
     * through START (a migrated save, a console command), resolve now rather
     * than shipping a Visitor with no identity. */
    if (p->gender > GENDER_GIRL) p->gender = (uint8_t)random(0, 2);

    /* Hungry and wanting attention, but in a clean room. */
    p->hunger      = EGG_HATCH_HUNGER;
    p->happiness   = EGG_HATCH_HAPPINESS;
    p->cleanliness = EGG_HATCH_CLEANLINESS;
    p->care_happy  = EGG_HATCH_HAPPINESS;
    p->care_fed    = EGG_HATCH_HUNGER;
    p->care_clean  = EGG_HATCH_CLEANLINESS;

    if (rtc_trusted()) { p->hatch_ts = rtc_now(); p->last_sim_ts = p->hatch_ts; }
    care_reset();                       /* no messes to greet it */
    journal_add(JM_HATCHED, 0, 0);
    pet_record_form();                  /* "How I grew up" starts HERE */
    /* The reveal and the first words own the next couple of minutes. */
    discipline_settle(MISCHIEF_SETTLE_HATCH_MS);

    ui_pet_set_egg(false, 0);
    ui_pet_set_form(FORM_BABY);
    ui_pet_set_baby_palette(p->egg_color);   /* it came out of THAT egg */
    ui_pet_play(PET_ANIM_HAPPY);
    persist_save(true);

    const uint32_t now = millis();
    if (was_surprise && s_reveal) {
        lv_label_set_text(s_reveal_lbl, p->gender == GENDER_BOY ? "It's a boy!"
                                                                : "It's a girl!");
        show_obj(s_reveal, true);
        s_reveal_until_ms = now + HATCH_REVEAL_MS;
        s_greet_at_ms     = s_reveal_until_ms + HATCH_GREET_DELAY_MS;
        if (!s_greet_at_ms) s_greet_at_ms = 1;   /* 0 is the "none pending" sentinel */
        Serial.printf("EGG: hatched! It's a %s - revealing before the first words\n",
                      p->gender == GENDER_BOY ? "boy" : "girl");
    } else {
        s_reveal_until_ms = 0;
        s_greet_at_ms     = now + HATCH_GREET_DELAY_MS;
        if (!s_greet_at_ms) s_greet_at_ms = 1;
        Serial.printf("EGG: hatched! A new Baby has arrived (%s, already known)\n",
                      p->gender == GENDER_BOY ? "boy" : "girl");
    }
}

/* Beat 2 -> beat 3. Driven from the 1 s refresh; the delays are seconds, so
 * a one-second resolution is exactly enough. */
static void hatch_sequence_tick(void)
{
    const uint32_t now = millis();

    if (s_reveal_until_ms && (int32_t)(now - s_reveal_until_ms) >= 0) {
        s_reveal_until_ms = 0;
        if (s_reveal) show_obj(s_reveal, false);
    }
    if (!s_greet_at_ms || (int32_t)(now - s_greet_at_ms) < 0) return;
    if (s_reveal_until_ms) return;          /* never over the reveal */

    s_greet_at_ms = 0;

    /* THE FIRST WORDS ALWAYS HAPPEN. The Phase 9 callback to a previous
     * Visitor ("They said you keep this place really clean!") used to REPLACE
     * them whenever there was any history, so on every device after the first
     * visit the Visitor never actually introduced itself. Both now happen, in
     * the order that makes sense: hello first, then the thing it heard about
     * you. The bubble cooldowns space them out on their own - the second is
     * queued, not spoken over the first. */
    ui_bubble_say_deferred(BUBBLE_T1_REACTION, dialogue_hatch_greeting());

    const char *ref = visitrec_previous_reference();
    if (ref) ui_bubble_say_deferred(BUBBLE_T2_MOOD, ref);
}

void scr_main_egg_refresh(void)
{
    const pet_state_t *p = pet_get();
    const bool is_egg = (p->stage == STAGE_EGG);

    if (!is_egg) hatch_sequence_tick();

    ui_pet_set_egg(is_egg, egg_display_palette(p));
    const bool choosing = is_egg && !p->egg_hatch_ts;
    if (s_egg_btn) show_obj(s_egg_btn, choosing);
    if (s_egg_lbl) show_obj(s_egg_lbl, is_egg);
    for (uint8_t i = 0; i <= EGG_PALETTE_COUNT; i++) {
        if (!s_swatch[i]) continue;
        show_obj(s_swatch[i], choosing);
        /* the picked one gets a white ring, so the choice is unmistakable */
        lv_obj_set_style_border_opa(s_swatch[i],
            (p->egg_choice == i) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    for (uint8_t i = 0; i < 3; i++) {
        if (!s_gender_btn[i]) continue;
        show_obj(s_gender_btn[i], choosing);
        lv_obj_set_style_border_opa(s_gender_btn[i],
            (p->gender_choice == i) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (!is_egg) return;

    if (!p->egg_hatch_ts) {
        lv_label_set_text(s_egg_lbl, "Pick a colour and boy or girl, then START");
        ui_pet_set_egg_progress(0.0f);
        return;
    }

    const uint32_t now = rtc_trusted() ? rtc_now() : (millis() / 1000);
    if (now >= p->egg_hatch_ts) { egg_hatch(); return; }

    const uint32_t left = p->egg_hatch_ts - now;
    ui_pet_set_egg_progress(1.0f - (float)left / (float)EGG_HATCH_SEC);
    char b[48];
    snprintf(b, sizeof(b), "Hatching in %lu:%02lu",
             (unsigned long)(left / 60), (unsigned long)(left % 60));
    lv_label_set_text(s_egg_lbl, b);
}

static uint32_t s_sleep_poke_ms;
static uint8_t  s_sleep_pokes;

static void pet_pressed_cb(lv_event_t *e)
{
    (void)e;
    const pet_state_t *p = pet_get();

    /* SLEEP OVERRIDES TAPPING. A poke gets a sleepy grumble and a small
     * wriggle; it never starts the awake reaction, never resumes wandering
     * and never takes the Visitor out of bed. */
    if (p->asleep) {
        const uint32_t now = millis();
        if (now - s_sleep_poke_ms < 2500) return;      /* no bubble spam */
        if (now - s_sleep_poke_ms > 20000) s_sleep_pokes = 0;
        s_sleep_poke_ms = now;
        if (s_sleep_pokes < 4) s_sleep_pokes++;

        ui_bubble_say(BUBBLE_T1_REACTION,
                      dialogue_sleepy_poke(s_sleep_pokes >= 4));
        return;                       /* stays asleep, stays in bed */
    }

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

    /* ROOM DARKNESS. Sits above the pet and the bed, below the Zs and the
     * bubble. A partial black overlay rather than a backlight change, so the
     * true-black background stays black and the menus are untouched. */
    s_dim = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_dim);
    lv_obj_set_size(s_dim, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_dim, 0, 0);
    lv_obj_set_style_bg_color(s_dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_dim, ROOM_DIM_OPA, 0);
    lv_obj_clear_flag(s_dim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);

    /* Zs live ABOVE the dim so they stay bright in a dark room. */
    s_zlayer = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_zlayer);
    lv_obj_set_size(s_zlayer, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_zlayer, 0, 0);
    lv_obj_clear_flag(s_zlayer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

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

    /* Egg label, ABOVE the raised shell. The pre-hatch screen is crowded now
     * (two colour rows, a gender row and START), so the shell was moved up to
     * EGG_ROOT_Y and this sits above it rather than over it. */
    s_egg_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_egg_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_egg_lbl, lv_color_hex(0xE8E8E8), 0);
    lv_label_set_long_mode(s_egg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_egg_lbl, BSP_LCD_W - 24);
    lv_obj_set_style_text_align(s_egg_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_egg_lbl, "");
    lv_obj_align(s_egg_lbl, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_add_flag(s_egg_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Colour swatches: six colours plus Surprise, as big coloured buttons
     * rather than a text list. Four on the top row, three centred beneath.
     * Every position comes from the EGG_* constants, whose derivation and
     * dead-band arithmetic are written out in config.h - so the spacing can
     * be checked by reading the numbers rather than by looking at the panel. */
    for (uint8_t i = 0; i <= EGG_PALETTE_COUNT; i++) {
        const bool row1 = (i >= 4);
        const uint8_t n_in_row = row1 ? 3 : 4;
        const lv_coord_t row_w = n_in_row * EGG_SW_W + (n_in_row - 1) * EGG_SW_GAP_X;
        const lv_coord_t x0 = (BSP_LCD_W - row_w) / 2;
        const lv_coord_t col = row1 ? (i - 4) : i;

        lv_obj_t *sw = lv_obj_create(s_scr);
        lv_obj_remove_style_all(sw);
        lv_obj_set_size(sw, EGG_SW_W, EGG_SW_H);
        lv_obj_set_pos(sw, x0 + col * (EGG_SW_W + EGG_SW_GAP_X),
                       row1 ? EGG_SW_ROW1_Y : EGG_SW_ROW0_Y);
        lv_obj_set_style_radius(sw, 12, 0);
        lv_obj_set_style_bg_color(sw,
            lv_color_hex(i < EGG_PALETTE_COUNT ? SWATCH[i] : 0x2A2A34), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(sw, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(sw, 5, 0);   /* thick ring when chosen */
        lv_obj_set_style_border_opa(sw, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(sw, egg_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (i == EGG_PALETTE_COUNT) {
            /* SURPRISE is drawn as the six colours themselves - stripes, not
             * a question mark. "?" read as "no colour"; six stripes read as
             * "it could be any of these", which is what it means. */
            const lv_coord_t bw = (EGG_SW_W - 12) / EGG_PALETTE_COUNT;
            for (uint8_t k = 0; k < EGG_PALETTE_COUNT; k++) {
                lv_obj_t *st = lv_obj_create(sw);
                lv_obj_remove_style_all(st);
                lv_obj_set_size(st, bw, EGG_SW_H - 22);
                lv_obj_set_pos(st, 6 + k * bw, 11);
                lv_obj_set_style_radius(st, 2, 0);
                lv_obj_set_style_bg_color(st, lv_color_hex(SWATCH[k]), 0);
                lv_obj_set_style_bg_opa(st, LV_OPA_COVER, 0);
                lv_obj_clear_flag(st, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            }
        }
        s_swatch[i] = sw;
    }

    /* Gender: Boy / Girl / Surprise. Same size class as the colours, its own
     * row, EGG_GENDER_GAP_ABOVE clear of them. Identity only - nothing this
     * row sets ever reaches gameplay, evolution or care difficulty. */
    {
        static const char *GNAME[3] = { "Boy", "Girl", "Surprise" };
        static const uint32_t GCOL[3] = { 0x7FA8E8, 0xE89ABF, 0x9AA6C4 };
        const lv_coord_t row_w = 3 * EGG_GENDER_W + 2 * EGG_GENDER_GAP_X;
        const lv_coord_t x0 = (BSP_LCD_W - row_w) / 2;

        for (uint8_t i = 0; i < 3; i++) {
            lv_obj_t *b = lv_obj_create(s_scr);
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, EGG_GENDER_W, EGG_GENDER_H);
            lv_obj_set_pos(b, x0 + i * (EGG_GENDER_W + EGG_GENDER_GAP_X),
                           EGG_GENDER_Y);
            lv_obj_set_style_radius(b, 12, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(GCOL[i]), 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(b, 5, 0);
            lv_obj_set_style_border_opa(b, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(b, gender_pick_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
            lv_obj_t *l = lv_label_create(b);
            lv_label_set_text(l, GNAME[i]);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0x101018), 0);
            lv_obj_center(l);
            s_gender_btn[i] = b;
        }
    }

    /* A big, unmissable START, EGG_START_DEAD_BAND clear of the gender row.
     * The gap matters AND the press-target rule matters: LVGL delivers
     * CLICKED to the object the press began on, so a touch that starts on a
     * selector and drifts down cannot fire START even at the boundary. */
    s_egg_btn = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_egg_btn);
    lv_obj_set_size(s_egg_btn, BSP_LCD_W - 80, EGG_START_H);
    lv_obj_set_pos(s_egg_btn, 40, EGG_START_Y);
    lv_obj_set_style_radius(s_egg_btn, 18, 0);
    lv_obj_set_style_bg_color(s_egg_btn, lv_color_hex(0x60D0A0), 0);
    lv_obj_set_style_bg_opa(s_egg_btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_egg_btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_egg_btn, egg_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(s_egg_btn);
    lv_label_set_text(el, "START");
    lv_obj_set_style_text_font(el, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(el, lv_color_hex(0x101018), 0);
    lv_obj_center(el);

    /* The identity reveal. A banner rather than a speech bubble on purpose:
     * bubbles are preemptible, cooldown-gated and anchored to a Visitor that
     * is mid-hatch-animation. This is the one moment that must not be
     * refused, moved or talked over. */
    s_reveal = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_reveal);
    lv_obj_set_size(s_reveal, BSP_LCD_W - 60, 74);
    lv_obj_align(s_reveal, LV_ALIGN_TOP_MID, 0, 330);   /* clear of the 150..310 pet box */
    lv_obj_set_style_radius(s_reveal, 18, 0);
    lv_obj_set_style_bg_color(s_reveal, lv_color_hex(0xF2F4F8), 0);
    lv_obj_set_style_bg_opa(s_reveal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_reveal, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reveal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *rv = lv_label_create(s_reveal);
    lv_label_set_text(rv, "");
    lv_obj_set_style_text_font(rv, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(rv, lv_color_hex(0x16161E), 0);
    lv_obj_center(rv);
    s_reveal_lbl = rv;      /* the banner is shown/hidden; the label is written */

    care_init(s_room_layer);
}

void scr_main_show(void)   { if (s_scr) lv_scr_load(s_scr); }
lv_obj_t *scr_main_obj(void)     { return s_scr; }
lv_obj_t *scr_main_overlay(void) { return s_pet_overlay; }

void scr_main_set_room_dark(bool dark)
{
    if (!s_dim) return;
    if (dark) lv_obj_clear_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
}

/* --- floating Zs ---------------------------------------------------------
 * One at a time, drifting up and fading. Deliberately sparse: a screen full
 * of Zs would be noise, and this is meant to read as quiet breathing. */
static void z_del_cb(lv_anim_t *a) { lv_obj_del((lv_obj_t *)a->user_data); }
static void z_y_cb(void *o, int32_t v)   { lv_obj_set_y((lv_obj_t *)o, (lv_coord_t)v); }
static void z_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

void scr_main_sleep_fx(void)
{
    if (!s_zlayer || !pet_get()->asleep) return;
    const uint32_t now = millis();
    if (now - s_last_z_ms < 2200) return;
    s_last_z_ms = now;

    lv_obj_t *z = lv_label_create(s_zlayer);
    lv_label_set_text(z, (random(0, 2)) ? "z" : "Z");
    lv_obj_set_style_text_font(z, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(z, lv_color_hex(0xE8F0FF), 0);
    const lv_coord_t x = BED_CX + (lv_coord_t)random(-6, 30);
    const lv_coord_t y0 = BED_CY - 74;
    lv_obj_set_pos(z, x, y0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, z);
    lv_anim_set_exec_cb(&a, z_y_cb);
    lv_anim_set_values(&a, y0, y0 - 54);
    lv_anim_set_time(&a, 2600);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, z);
    lv_anim_set_exec_cb(&a, z_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 2600);
    a.user_data = z;
    lv_anim_set_ready_cb(&a, z_del_cb);
    lv_anim_start(&a);
}

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
