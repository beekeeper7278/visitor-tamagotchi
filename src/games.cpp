/* games - shared shell + Higher/Lower. See games.h. */

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "scr_main.h"
#include "menu.h"
#include "pages.h"
#include "gamerec.h"
#include "forms.h"
#include "evolve.h"
#include "journal.h"
#include "dialogue.h"
#include "games.h"
#include "diag.h"
#include <math.h>

/* ==========================================================================
 * shared shell
 * ======================================================================= */

static lv_obj_t *s_scr;          /* the game screen; deleted on exit      */
static lv_obj_t *s_body;         /* content area, rebuilt per phase       */
static uint8_t   s_game;
static bool      s_active;
static uint16_t  s_score;
static uint32_t  s_elapsed_ms;

/* Phase 10 owns audio. These exist so the call sites are already correct. */
void games_sfx(game_sfx_t s) { (void)s; }

/* Stage 0..3 = Baby/Kid/Teen/Adult. An Egg plays with Baby settings - it
 * should not be locked out of games for lacking a birthday. */
static uint8_t gstage(void)
{
    const uint8_t st = pet_get()->stage;
    if (st <= STAGE_BABY) return 0;
    return (uint8_t)(st - STAGE_BABY);
}

static lv_obj_t *g_btn(lv_obj_t *par, const char *txt, lv_coord_t x, lv_coord_t y,
                       lv_coord_t w, lv_coord_t h, uint32_t col, uint32_t fg,
                       lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_obj_create(par);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *g_label(lv_obj_t *par, const char *txt, lv_coord_t y,
                         const lv_font_t *font, uint32_t col)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, BSP_LCD_W - 40);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

/* --- mini Visitor -------------------------------------------------------
 * A small, self-contained Visitor built from the SAME pet_form_t the real
 * renderer uses, so it takes the live form's colours and reads as the actual
 * pet rather than a generic blob. ui_pet's renderer is a single global
 * instance and cannot be spawned twice, hence this compact standalone
 * version - it is deliberately simple, because at 24-76 px eyes and a mouth
 * are all that survive anyway.
 *
 * The returned container is the touch target; every child is
 * non-clickable so taps land on the parent. */
static lv_obj_t *mini_shape(lv_obj_t *par, lv_coord_t w, lv_coord_t h,
                            lv_coord_t x, lv_coord_t y, lv_coord_t r, uint32_t col)
{
    lv_obj_t *o = lv_obj_create(par);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mini_pet(lv_obj_t *par, lv_coord_t size)
{
    const pet_form_t *f = forms_get(pet_get()->form_id);

    lv_obj_t *c = lv_obj_create(par);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, size, size);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t bh = (lv_coord_t)(size * 0.92f);
    mini_shape(c, size, bh, 0, size - bh, size / 2, f->c_body);
    /* belly only when there is room for it to read as anything */
    if (size >= 40) {
        const lv_coord_t lw = (lv_coord_t)(size * 0.56f), lh = (lv_coord_t)(bh * 0.38f);
        mini_shape(c, lw, lh, (size - lw) / 2, size - lh - (lv_coord_t)(size * 0.06f),
                   lh / 2, f->c_belly);
    }

    const lv_coord_t er = size >= 40 ? (lv_coord_t)(size * 0.15f)
                                     : (lv_coord_t)(size * 0.18f);
    const lv_coord_t ey = (lv_coord_t)(size * 0.40f);
    mini_shape(c, er, (lv_coord_t)(er * 1.2f), (lv_coord_t)(size * 0.26f), ey,
               er, f->c_eye);
    mini_shape(c, er, (lv_coord_t)(er * 1.2f),
               (lv_coord_t)(size * 0.74f - er), ey, er, f->c_eye);

    if (size >= 34) {                       /* a little smile */
        const lv_coord_t mw = (lv_coord_t)(size * 0.26f);
        mini_shape(c, mw, 3, (size - mw) / 2, (lv_coord_t)(size * 0.66f), 2, f->c_eye);
        const lv_coord_t cw = (lv_coord_t)(size * 0.16f);
        lv_obj_t *cl = mini_shape(c, cw, (lv_coord_t)(cw * 0.6f),
                                  (lv_coord_t)(size * 0.08f),
                                  (lv_coord_t)(size * 0.56f), cw, f->c_accent);
        lv_obj_set_style_bg_opa(cl, LV_OPA_70, 0);
        lv_obj_t *cr = mini_shape(c, cw, (lv_coord_t)(cw * 0.6f),
                                  (lv_coord_t)(size * 0.92f - cw),
                                  (lv_coord_t)(size * 0.56f), cw, f->c_accent);
        lv_obj_set_style_bg_opa(cr, LV_OPA_70, 0);
    }
    return c;
}

static uint32_t game_colour(uint8_t g)
{
    switch (g) {
        case GAME_HILO:   return 0xF2C14E;
        case GAME_REACT:  return 0xFF8A75;
        case GAME_MEMORY: return 0x5FCBB4;
        default:          return 0xA894EE;
    }
}

/* Forward declarations: body_reset() invalidates every per-game pointer, and
 * those live with their own games further down. */
static lv_obj_t *s_hl_num, *s_hl_info, *s_hl_reveal, *s_hl_hi, *s_hl_lo;
static lv_obj_t *s_hl_pet, *s_hl_bub, *s_hl_bub_txt;
static lv_timer_t *s_hl_timer;
static lv_obj_t *s_rx_target, *s_rx_info;
static lv_obj_t *s_mz_ball, *s_mz_info;
static lv_obj_t *s_mem_pad[9], *s_mem_info;

static void body_reset(void)
{
    /* Everything below lived inside s_body; clearing the pointers stops a
     * late callback writing through a dangling one. */
    s_hl_pet = s_hl_bub = s_hl_bub_txt = nullptr;
    s_hl_num = s_hl_info = s_hl_reveal = s_hl_hi = s_hl_lo = nullptr;
    s_rx_target = s_rx_info = nullptr;
    s_mz_ball = s_mz_info = nullptr;
    s_mem_info = nullptr;
    memset(s_mem_pad, 0, sizeof(s_mem_pad));
    if (s_body) lv_obj_del(s_body);
    s_body = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_size(s_body, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_pos(s_body, 0, 0);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
}

/* Reaction tuning lives up here because the intro screen advertises the
 * round length and target before the game section is reached. */
/* RETUNED ON HARDWARE: the first timed version was far too easy. Targets
 * respawn the instant you tag one, so a LONGER round simply produces a
 * bigger score - it never makes the target harder to reach. A 28 s Kid round
 * against a target of 10 was over before it began.
 *
 * So the round length is the lever that came down, not the targets: those
 * are the original design values and worth keeping. Rounds are now short
 * enough that the target is a genuine race, and the exposure was tightened
 * with a steeper in-round ramp so misses actually cost you.
 *
 * Rough intent per stage - roughly one tag per second sustained:
 *   Baby 12 s / 5   easy by design, it is a toddler's game
 *   Kid  15 s / 10  a real contest
 *   Teen 18 s / 15  demands few misses
 *   Adult 22 s / 22 needs ~1 tag/sec at sub-500 ms exposure by the end */
static const uint16_t RX_MS[4]     = { 1400, 1050, 800, 650 };  /* start */
static const uint8_t  RX_TARGET[4] = { 5, 10, 15, 22 };         /* design values */
static const uint16_t RX_ROUND_S[4]= { 12, 15, 18, 22 };        /* was 20/28/36/45 */
#define RX_RAMP 0.60f     /* exposure at the end, as a fraction of the start */

static void show_intro(void);
static void show_result(void);
static void start_play(void);

static void exit_cb(lv_event_t *e);
static void again_cb(lv_event_t *e) { (void)e; start_play(); }
static void start_cb(lv_event_t *e) { (void)e; start_play(); }

/* --- the four games plug in here ---------------------------------------- */
static void hilo_start(void);
static void react_start(void);
static void memory_start(void);
static void maze_start(void);
static void games_cleanup_timers(void);

static void start_play(void)
{
    /* Cost of playing, applied once per attempt [SPEC]. */
    pet_state_t *p = pet_mutable();
    p->energy = p->energy < 5.0f ? 0.0f : p->energy - 5.0f;
    p->hunger = p->hunger < 3.0f ? 0.0f : p->hunger - 3.0f;

    s_score = 0;
    s_elapsed_ms = 0;
    games_sfx(SFX_START);

    switch (s_game) {
        case GAME_HILO:   hilo_start();   break;
        case GAME_REACT:  react_start();  break;
        case GAME_MEMORY: memory_start(); break;
        default:          maze_start();   break;
    }
}

/* The console standing in for the player's finger on Start, through the SAME
 * entry point the button uses. Without it there is no way to reach a round
 * from the console at all - games_launch() only opens the intro screen - so
 * the maze could not be exercised headlessly. Exactly the reason the egg's
 * ':' command was rewired to call the real START path. */
void games_press_start(void)
{
    if (!s_active) { Serial.println("no game open - launch one first"); return; }
    start_play();
}

/* Happiness, the multiplier, and the record write all happen here so no
 * individual game can forget one of them. */
static float s_happy_awarded;
static float s_multiplier;

static void finish_game(uint16_t score, uint32_t ms, float happy)
{
    s_score = score;
    s_elapsed_ms = ms;

    const uint32_t now = rtc_trusted() ? rtc_now() : (millis() / 1000);
    const uint16_t prev_best = gamerec_get()->best[s_game];
    const bool first_play = (gamerec_get()->plays[s_game] == 0);
    s_multiplier = gamerec_record_play(s_game, score, ms, now);
    if (first_play)            journal_add(JM_FIRST_GAME, s_game, 0);
    else if (score > prev_best) journal_add(JM_RECORD, s_game, score);

    float h = happy * s_multiplier;
    if (h < 0.0f) h = 0.0f;          /* playing must never cost happiness */
    s_happy_awarded = h;

    pet_state_t *p = pet_mutable();
    p->games_played++;      /* engagement feeds the care score */
    p->happiness += h;
    if (p->happiness > 100.0f) p->happiness = 100.0f;

    Serial.printf("GAME %s: score %u  happy +%.1f (x%.2f)\n",
                  gamerec_name(s_game), score, h, s_multiplier);
    show_result();
}

static void show_intro(void)
{
    body_reset();
    const gamerec_t *r = gamerec_get();

    g_label(s_body, gamerec_name(s_game), 26, &lv_font_montserrat_28,
            game_colour(s_game));

    char b[160];
    if (s_game == GAME_MAZE) {
        if (r->maze_best_ms)
            snprintf(b, sizeof(b), "Played %u times\nBest time %lu.%02lus",
                     r->plays[s_game], (unsigned long)(r->maze_best_ms / 1000),
                     (unsigned long)((r->maze_best_ms % 1000) / 10));
        else
            snprintf(b, sizeof(b), "Played %u times\nNo best time yet",
                     r->plays[s_game]);
    } else {
        if (s_game == GAME_REACT)
            snprintf(b, sizeof(b),
                     "Played %u times   Best %u\n%u seconds to tag %u!",
                     r->plays[s_game], r->best[s_game],
                     RX_ROUND_S[gstage()], RX_TARGET[gstage()]);
        else
            snprintf(b, sizeof(b), "Played %u times\nBest score %u",
                     r->plays[s_game], r->best[s_game]);
    }
    g_label(s_body, b, 74, &lv_font_montserrat_20, 0xB0B8C8);

    /* Warn about the repetition penalty BEFORE they play, not after. */
    const uint32_t now = rtc_trusted() ? rtc_now() : (millis() / 1000);
    if (gamerec_pending_multiplier(s_game, now) < 1.0f)
        g_label(s_body, "You've played this a lot - less happiness this time!",
                136, &lv_font_montserrat_20, 0xE8A33D);

    g_btn(s_body, "START", 44, 250, BSP_LCD_W - 88, 76,
          game_colour(s_game), 0x101018, start_cb, NULL);
    g_btn(s_body, "Back", 44, 344, BSP_LCD_W - 88, 60,
          0x2A2A34, 0xD0D4DC, exit_cb, NULL);
}

static void show_result(void)
{
    games_cleanup_timers();
    body_reset();
    const gamerec_t *r = gamerec_get();

    g_label(s_body, "Result", 20, &lv_font_montserrat_28, game_colour(s_game));

    char b[220];
    if (s_game == GAME_MAZE) {
        snprintf(b, sizeof(b), "Your time: %lu.%02lus\nBest: %lu.%02lus",
                 (unsigned long)(s_elapsed_ms / 1000),
                 (unsigned long)((s_elapsed_ms % 1000) / 10),
                 (unsigned long)(r->maze_best_ms / 1000),
                 (unsigned long)((r->maze_best_ms % 1000) / 10));
    } else {
        snprintf(b, sizeof(b), "Your score: %u\nBest ever: %u", s_score,
                 r->best[s_game]);
    }
    g_label(s_body, b, 68, &lv_font_montserrat_20, 0xE8E8E8);

    snprintf(b, sizeof(b), "Happiness +%d%s", (int)(s_happy_awarded + 0.5f),
             s_multiplier < 1.0f ? "  (repeat penalty)" : "");
    g_label(s_body, b, 132, &lv_font_montserrat_20, 0x60D0A0);

    /* The Visitor reacts on its own screen too, so the result is not just
     * numbers on a card. */
    g_label(s_body, s_happy_awarded >= 14.0f ? "\"That was SO fun!\""
            : s_happy_awarded >= 8.0f ? "\"Good game!\"" : "\"Let's play again!\"",
            172, &lv_font_montserrat_20, 0xB0B8C8);

    g_btn(s_body, "Play Again", 44, 240, BSP_LCD_W - 88, 76,
          game_colour(s_game), 0x101018, again_cb, NULL);
    g_btn(s_body, "Back to Games", 44, 334, BSP_LCD_W - 88, 68,
          0x2A2A34, 0xD0D4DC, exit_cb, NULL);
}

bool games_active(void) { return s_active; }

static void games_exit(void)
{
    if (!s_active) return;
    games_cleanup_timers();
    s_active = false;

    /* Restore everything the game suspended. Missing one of these is how a
     * game leaves the Visitor frozen or permanently mute. */
    ui_bubble_set_suppressed(false);
    ui_pet_set_wander(true);
    if (ui_pet_current() != PET_ANIM_SLEEPING) ui_pet_play(PET_ANIM_IDLE);

    lv_obj_t *dead = s_scr;
    s_scr = nullptr; s_body = nullptr;
    scr_main_show();
    menu_open();
    menu_goto(PAGE_GAMES);
    if (dead) lv_obj_del(dead);

    ui_bubble_say(BUBBLE_T1_REACTION, dialogue_game_done());
    Serial.println("GAME exit -> Games page");
}

static void exit_cb(lv_event_t *e) { (void)e; games_exit(); }
void games_force_exit(void) { games_exit(); }

void games_launch(uint8_t game)
{
    if (s_active || game >= GAME_COUNT) return;
    if (pet_get()->stage == STAGE_EGG) {
        Serial.println("game ignored: still an egg");
        return;
    }
    s_game  = game;
    s_active = true;

    /* Suspend everything that would fight the game for the screen or touch. */
    ui_bubble_set_suppressed(true);
    ui_pet_set_wander(false);
    if (menu_is_open()) menu_close();

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    s_body = nullptr;

    show_intro();
    lv_scr_load(s_scr);
    Serial.printf("GAME launch: %s (stage %s)\n",
                  gamerec_name(game), pet_stage_name(pet_get()->stage));
}

/* ==========================================================================
 * GAME 1 - Higher / Lower
 * ======================================================================= */

static const uint8_t HL_MAX[4] = { 5, 9, 15, 20 };   /* Baby/Kid/Teen/Adult */
#define HL_ROUNDS 10

static uint8_t  s_hl_round, s_hl_shown, s_hl_hidden;
static int16_t  s_hl_score;
static lv_coord_t s_hl_pet_x, s_hl_pet_y;

/* --- the Visitor's reactions --------------------------------------------
 * The pet is a player in this game, not decoration: it hops when you are
 * right, shakes its head when you are wrong, and talks the whole way
 * through. ui_bubble is suppressed during games and anchored to the pet on
 * the main screen, so the game carries its own small bubble. */

static void hl_say(const char *txt)
{
    if (!s_hl_bub) return;
    lv_label_set_text(s_hl_bub_txt, txt);
    lv_obj_clear_flag(s_hl_bub, LV_OBJ_FLAG_HIDDEN);
}

static void hl_anim_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, (lv_coord_t)v); }
static void hl_anim_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, (lv_coord_t)v); }

static void hl_hop(void)
{
    if (!s_hl_pet) return;
    lv_anim_del(s_hl_pet, NULL);
    lv_obj_set_pos(s_hl_pet, s_hl_pet_x, s_hl_pet_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_hl_pet);
    lv_anim_set_exec_cb(&a, hl_anim_y);
    lv_anim_set_values(&a, s_hl_pet_y, s_hl_pet_y - 20);
    lv_anim_set_time(&a, 150);
    lv_anim_set_playback_time(&a, 150);
    lv_anim_set_repeat_count(&a, 2);
    lv_anim_start(&a);
}

static void hl_shake(void)
{
    if (!s_hl_pet) return;
    lv_anim_del(s_hl_pet, NULL);
    lv_obj_set_pos(s_hl_pet, s_hl_pet_x, s_hl_pet_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_hl_pet);
    lv_anim_set_exec_cb(&a, hl_anim_x);
    lv_anim_set_values(&a, s_hl_pet_x - 9, s_hl_pet_x + 9);
    lv_anim_set_time(&a, 80);
    lv_anim_set_playback_time(&a, 80);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_start(&a);
}

static const char *HL_YES[] = { "Yes!", "You got it!", "Nice one!", "Hehe!" };
static const char *HL_NO[]  = { "Aww...", "Nope!", "So close!", "Oh no!" };
static const char *HL_ASK[] = { "Higher or lower?", "What do you think?",
                                "Hmm... guess!", "Your turn!" };

static void hl_next_round(void);

static void hl_answer(lv_event_t *e)
{
    const int guess_higher = (int)(intptr_t)lv_event_get_user_data(e);
    const uint8_t hi = HL_MAX[gstage()];
    s_hl_hidden = (uint8_t)random(1, hi + 1);

    char b[96];
    if (s_hl_hidden == s_hl_shown) {
        snprintf(b, sizeof(b), "I had %u", s_hl_hidden);
        hl_say("THE SAME! Spooky.");
        hl_hop();
        games_sfx(SFX_TICK);
    } else {
        const bool higher = s_hl_hidden > s_hl_shown;
        const bool right  = (higher == (guess_higher != 0));
        s_hl_score += right ? 1 : -1;
        snprintf(b, sizeof(b), "I had %u", s_hl_hidden);
        if (right) { hl_say(HL_YES[random(0, 4)]); hl_hop();   }
        else       { hl_say(HL_NO[random(0, 4)]);  hl_shake(); }
        games_sfx(right ? SFX_HIT : SFX_MISS);
    }
    lv_label_set_text(s_hl_reveal, b);

    s_hl_round++;
    if (s_hl_round >= HL_ROUNDS) {
        /* +6 + score, clamped 4..18, so a bad run still pays something and
         * playing can never be a net happiness loss. */
        float h = 6.0f + (float)s_hl_score;
        if (h < 4.0f)  h = 4.0f;
        if (h > 18.0f) h = 18.0f;
        /* Report a floor of 0 rather than a negative score: the records are
         * "best ever", and a negative best is meaningless. */
        const uint16_t reported = (uint16_t)(s_hl_score < 0 ? 0 : s_hl_score);
        finish_game(reported, 0, h);
        return;
    }
    /* TRACKED, deliberately. This used to be an untracked one-shot: quitting
     * during the ~950 ms reveal delay left it running, and it then fired
     * hl_next_round() into objects the screen teardown had already freed.
     * Every game timer now lives in games_cleanup_timers(). */
    if (s_hl_timer) lv_timer_del(s_hl_timer);
    s_hl_timer = lv_timer_create([](lv_timer_t *x) {
        s_hl_timer = nullptr;
        lv_timer_del(x);
        hl_next_round();
    }, 950, NULL);
    lv_timer_set_repeat_count(s_hl_timer, 1);
}

static void hl_next_round(void)
{
    const uint8_t hi = HL_MAX[gstage()];
    s_hl_shown = (uint8_t)random(1, hi + 1);

    char b[64];
    snprintf(b, sizeof(b), "Round %u of %u    Score %d",
             (unsigned)(s_hl_round + 1), (unsigned)HL_ROUNDS, (int)s_hl_score);
    lv_label_set_text(s_hl_info, b);
    snprintf(b, sizeof(b), "%u", s_hl_shown);
    lv_label_set_text(s_hl_num, b);
    lv_label_set_text(s_hl_reveal, "");
    hl_say(HL_ASK[random(0, 4)]);
    if (s_hl_pet) { lv_anim_del(s_hl_pet, NULL);
                    lv_obj_set_pos(s_hl_pet, s_hl_pet_x, s_hl_pet_y); }

    /* At the extremes one answer is impossible. Disable it and say so
     * playfully, rather than letting a child pick a guaranteed loss. */
    const bool at_min = (s_hl_shown == 1);
    const bool at_max = (s_hl_shown == hi);
    lv_obj_set_style_bg_opa(s_hl_lo, at_min ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_hl_hi, at_max ? LV_OPA_30 : LV_OPA_COVER, 0);
    if (at_min) lv_obj_clear_flag(s_hl_lo, LV_OBJ_FLAG_CLICKABLE);
    else        lv_obj_add_flag(s_hl_lo, LV_OBJ_FLAG_CLICKABLE);
    if (at_max) lv_obj_clear_flag(s_hl_hi, LV_OBJ_FLAG_CLICKABLE);
    else        lv_obj_add_flag(s_hl_hi, LV_OBJ_FLAG_CLICKABLE);
    /* The Visitor explains the locked button itself, so the impossible
     * choice reads as a joke it is in on rather than a greyed-out control. */
    if (at_min) hl_say("It can't be lower than 1!");
    if (at_max) hl_say("Nothing's higher than that!");
}

static void hilo_start(void)
{
    body_reset();
    s_hl_round = 0; s_hl_score = 0;

    char b[64];
    snprintf(b, sizeof(b), "Numbers 1 to %u", HL_MAX[gstage()]);
    g_label(s_body, b, 16, &lv_font_montserrat_20, 0x8890A0);

    s_hl_info = g_label(s_body, "", 40, &lv_font_montserrat_20, 0xB0B8C8);

    /* speech bubble, above the Visitor */
    s_hl_bub = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_hl_bub);
    lv_obj_set_size(s_hl_bub, 224, 46);
    lv_obj_set_pos(s_hl_bub, 72, 68);
    lv_obj_set_style_radius(s_hl_bub, 14, 0);
    lv_obj_set_style_bg_color(s_hl_bub, lv_color_hex(0xF2F4F8), 0);
    lv_obj_set_style_bg_opa(s_hl_bub, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_hl_bub, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_hl_bub_txt = lv_label_create(s_hl_bub);
    lv_label_set_long_mode(s_hl_bub_txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hl_bub_txt, 204);
    lv_obj_set_style_text_align(s_hl_bub_txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_hl_bub_txt, lv_color_hex(0x16161E), 0);
    lv_obj_center(s_hl_bub_txt);

    /* the number on the left, the Visitor on the right, side by side */
    s_hl_num = lv_label_create(s_body);
    lv_obj_set_style_text_font(s_hl_num, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_hl_num, lv_color_hex(0xF2C14E), 0);
    lv_label_set_text(s_hl_num, "?");
    lv_obj_align(s_hl_num, LV_ALIGN_TOP_LEFT, 86, 148);

    s_hl_pet_x = 200; s_hl_pet_y = 126;
    s_hl_pet = mini_pet(s_body, 76);
    lv_obj_set_pos(s_hl_pet, s_hl_pet_x, s_hl_pet_y);

    s_hl_reveal = g_label(s_body, "", 214, &lv_font_montserrat_20, 0xE8E8E8);

    s_hl_hi = g_btn(s_body, "HIGHER", 34, 250, 300, 76, 0x60D0A0, 0x101018,
                    hl_answer, (void *)(intptr_t)1);
    s_hl_lo = g_btn(s_body, "LOWER", 34, 334, 300, 76, 0x7FA8E8, 0x101018,
                    hl_answer, (void *)(intptr_t)0);
    g_btn(s_body, "Quit", 252, 4, 100, 38, 0x2A2A34, 0xB0B8C8, exit_cb, NULL);

    hl_next_round();
}

/* ==========================================================================
 * GAME 2 - Reaction / Tag
 *
 * TIMED ROUND, not a fixed number of appearances. You tag as many Visitors
 * as you can before the clock runs out.
 *
 * This resolves the Adult-target problem properly. The original design paired
 * "20 appearances" with an Adult target of 22, which was unreachable - one
 * point per appearance caps the score at 20. I previously lowered the Adult
 * target to 18 as the smallest fix available inside that model. A timed
 * round removes the cap entirely, so the ORIGINAL target of 22 is restored;
 * the earlier correction is no longer needed and has been reverted.
 *
 * Difficulty grows in three ways as the Visitor does:
 *   - longer rounds        (20 -> 45 s): more time, so bigger scores
 *   - higher targets       (5 -> 22)   : more to beat
 *   - shorter exposure     (1600 -> 700 ms)
 * and WITHIN a round the exposure shortens toward RX_RAMP of its starting
 * value, so a round that begins gently ends fast. A round of constant
 * difficulty stops being interesting about ten seconds in.
 * ======================================================================= */


/* 5x6 grid, inset so a target can never land under the HUD strip or the
 * bottom edge. +/-8 px jitter stops it feeling mechanical. */
#define RX_COLS 5
#define RX_ROWS 6
#define RX_X0   24
#define RX_Y0   96
#define RX_CW   64
#define RX_CH   46
#define RX_SIZE 76     /* a Visitor, and a much easier tap */

static lv_timer_t *s_rx_timer, *s_rx_clock;
static uint16_t   s_rx_shown, s_rx_hits, s_rx_miss;
static uint32_t   s_rx_end_ms, s_rx_dur_ms;

static void rx_place(void);
static void rx_finish(void);

/* Exposure shrinks as the round progresses - the ramp is what stops the
 * difficulty flat-lining. */
static uint16_t rx_exposure(void)
{
    const uint32_t now = millis();
    float progress = (s_rx_end_ms > now)
        ? 1.0f - (float)(s_rx_end_ms - now) / (float)s_rx_dur_ms : 1.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    const float f = 1.0f - (1.0f - RX_RAMP) * progress;
    return (uint16_t)(RX_MS[gstage()] * f);
}

static void rx_timeout(lv_timer_t *t)
{
    (void)t;
    s_rx_timer = nullptr;
    s_rx_miss++;
    games_sfx(SFX_MISS);
    rx_place();
}

static void rx_hit(lv_event_t *e)
{
    (void)e;
    s_rx_hits++;
    games_sfx(SFX_HIT);
    rx_place();
}

static void rx_tick(lv_timer_t *t)
{
    (void)t;
    if ((int32_t)(millis() - s_rx_end_ms) >= 0) { rx_finish(); return; }
    const uint32_t left = (s_rx_end_ms - millis() + 999) / 1000;
    char b[80];
    snprintf(b, sizeof(b), "%lus left    tagged %u    beat %u",
             (unsigned long)left, s_rx_hits, RX_TARGET[gstage()]);
    if (s_rx_info) lv_label_set_text(s_rx_info, b);
}

static void rx_finish(void)
{
    if (s_rx_timer) { lv_timer_del(s_rx_timer); s_rx_timer = nullptr; }
    if (s_rx_clock) { lv_timer_del(s_rx_clock); s_rx_clock = nullptr; }
    if (s_rx_target) lv_obj_add_flag(s_rx_target, LV_OBJ_FLAG_HIDDEN);

    const uint8_t tgt = RX_TARGET[gstage()];
    float h = 8.0f + 0.5f * (float)s_rx_hits;
    if (h > 20.0f) h = 20.0f;
    if (s_rx_hits >= tgt) h += 5.0f;        /* beat the Visitor */
    games_sfx(s_rx_hits >= tgt ? SFX_WIN : SFX_LOSE);
    finish_game(s_rx_hits, 0, h);
}

static void rx_place(void)
{
    if (s_rx_timer) { lv_timer_del(s_rx_timer); s_rx_timer = nullptr; }
    if (!s_rx_target) return;
    if ((int32_t)(millis() - s_rx_end_ms) >= 0) { rx_finish(); return; }

    s_rx_shown++;

    const int col = random(0, RX_COLS), row = random(0, RX_ROWS);
    const int jx = random(-8, 9), jy = random(-8, 9);
    const lv_coord_t x = RX_X0 + col * RX_CW + (RX_CW - RX_SIZE) / 2 + jx;
    const lv_coord_t y = RX_Y0 + row * RX_CH + (RX_CH - RX_SIZE) / 2 + jy;

    lv_obj_set_pos(s_rx_target, x, y);
    lv_obj_clear_flag(s_rx_target, LV_OBJ_FLAG_HIDDEN);

    s_rx_timer = lv_timer_create(rx_timeout, rx_exposure(), NULL);
    lv_timer_set_repeat_count(s_rx_timer, 1);
}

static void react_start(void)
{
    body_reset();
    s_rx_shown = s_rx_hits = s_rx_miss = 0;
    s_rx_dur_ms = (uint32_t)RX_ROUND_S[gstage()] * 1000UL;
    s_rx_end_ms = millis() + s_rx_dur_ms;

    s_rx_info = g_label(s_body, "", 52, &lv_font_montserrat_20, 0xB0B8C8);

    /* Tag the Visitor itself, not an abstract dot. */
    s_rx_target = mini_pet(s_body, RX_SIZE);
    lv_obj_add_flag(s_rx_target, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_rx_target, rx_hit, LV_EVENT_PRESSED, NULL);

    /* Quit at the top: the grid reaches far enough down that a bottom button
     * would sit under the targets. */
    g_btn(s_body, "Quit", 252, 4, 100, 38, 0x2A2A34, 0xB0B8C8, exit_cb, NULL);

    s_rx_clock = lv_timer_create(rx_tick, 200, NULL);
    rx_tick(nullptr);
    rx_place();
}

/* ==========================================================================
 * GAME 3 - Memory
 *
 * A mistake does NOT end the round. The player always enters the full
 * sequence and is scored on how much of it was right - a five-year-old who
 * slips on step two should still get credit for the rest.
 * ======================================================================= */

static const uint8_t MEM_LEN[4]    = { 3, 5, 7, 10 };
static const uint8_t MEM_COLORS[4] = { 3, 4, 5, 6 };
static const uint8_t MEM_GRID[4]   = { 2, 2, 3, 3 };     /* NxN */
static const uint16_t MEM_ON[4]    = { 900, 700, 700, 700 };
static const uint16_t MEM_OFF[4]   = { 300, 250, 250, 250 };

static const uint32_t MEM_PAL[6] = { 0xFF8A75, 0xF2C14E, 0x5FCBB4,
                                     0xA894EE, 0x7FA8E8, 0x8FCB9B };

static uint8_t   s_mem_seq[12], s_mem_len, s_mem_step, s_mem_in, s_mem_right;
static bool      s_mem_playing;      /* showing the sequence */
static lv_timer_t *s_mem_timer;

static void mem_show_step(lv_timer_t *t);

static void mem_begin_show(void)
{
    s_mem_playing = true;
    s_mem_step = 0;
    lv_label_set_text(s_mem_info, "Watch...");
    if (s_mem_timer) lv_timer_del(s_mem_timer);
    s_mem_timer = lv_timer_create(mem_show_step, 500, NULL);
}

/* Alternates ON and OFF phases using one timer whose period changes. */
static void mem_show_step(lv_timer_t *t)
{
    static bool on = false;
    on = !on;

    for (uint8_t i = 0; i < 9; i++)
        if (s_mem_pad[i]) lv_obj_set_style_bg_opa(s_mem_pad[i], LV_OPA_30, 0);

    const uint8_t st = gstage();
    if (on) {
        if (s_mem_step >= s_mem_len) {
            lv_timer_del(t); s_mem_timer = nullptr;
            s_mem_playing = false;
            s_mem_in = 0; s_mem_right = 0;
            lv_label_set_text(s_mem_info, "Your turn!");
            for (uint8_t i = 0; i < 9; i++)
                if (s_mem_pad[i]) lv_obj_set_style_bg_opa(s_mem_pad[i], LV_OPA_COVER, 0);
            return;
        }
        const uint8_t pad = s_mem_seq[s_mem_step];
        lv_obj_set_style_bg_opa(s_mem_pad[pad], LV_OPA_COVER, 0);
        games_sfx(SFX_TICK);
        lv_timer_set_period(t, MEM_ON[st]);
        s_mem_step++;
    } else {
        lv_timer_set_period(t, MEM_OFF[st]);
    }
}

static void mem_tap(lv_event_t *e)
{
    if (s_mem_playing) return;
    const uint8_t pad = (uint8_t)(intptr_t)lv_event_get_user_data(e);

    if (pad == s_mem_seq[s_mem_in]) { s_mem_right++; games_sfx(SFX_HIT); }
    else                             { games_sfx(SFX_MISS); }
    s_mem_in++;

    char b[64];
    snprintf(b, sizeof(b), "%u / %u", s_mem_in, s_mem_len);
    lv_label_set_text(s_mem_info, b);

    if (s_mem_in >= s_mem_len) {
        const float acc = (float)s_mem_right / (float)s_mem_len;
        float h;
        if (acc >= 0.999f)     h = 20.0f;
        else if (acc >= 0.75f) h = 14.0f;
        else if (acc >= 0.50f) h = 8.0f;
        else                   h = 4.0f;
        if (acc >= 0.999f) games_sfx(SFX_WIN);
        finish_game(s_mem_right, 0, h);
    }
}

static void memory_start(void)
{
    body_reset();
    const uint8_t st = gstage();
    const uint8_t n = MEM_GRID[st], colors = MEM_COLORS[st];
    s_mem_len = MEM_LEN[st];

    for (uint8_t i = 0; i < s_mem_len; i++)
        s_mem_seq[i] = (uint8_t)random(0, n * n);

    s_mem_info = g_label(s_body, "Watch...", 16, &lv_font_montserrat_28, 0x5FCBB4);

    /* Colourful shapes, not numbered buttons: circles and rounded squares in
     * the palette, so the language stays playful. */
    const lv_coord_t pad = 12;
    const lv_coord_t sz = (BSP_LCD_W - 40 - (n - 1) * pad) / n;
    const lv_coord_t x0 = (BSP_LCD_W - (n * sz + (n - 1) * pad)) / 2;
    const lv_coord_t y0 = 90;

    memset(s_mem_pad, 0, sizeof(s_mem_pad));
    for (uint8_t i = 0; i < n * n; i++) {
        lv_obj_t *b = lv_obj_create(s_body);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, sz, sz);
        lv_obj_set_pos(b, x0 + (i % n) * (sz + pad), y0 + (i / n) * (sz + pad));
        lv_obj_set_style_bg_color(b, lv_color_hex(MEM_PAL[i % colors]), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
        /* alternate circles and rounded squares for shape variety */
        lv_obj_set_style_radius(b, (i % 2) ? LV_RADIUS_CIRCLE : 14, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(b, mem_tap, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        s_mem_pad[i] = b;
    }

    g_btn(s_body, "Quit", 34, 402, 300, 40, 0x2A2A34, 0xB0B8C8, exit_cb, NULL);
    mem_begin_show();
}

/* ==========================================================================
 * GAME 4 - Tilt Maze
 *
 * AUTHORED OFFLINE, NOT GENERATED ON DEVICE. Each template was produced by
 * randomised Prim's on the host and then VERIFIED by breadth-first search -
 * both as authored and mirrored - before being pasted in. An on-device
 * generator would have to prove solvability at runtime, which is a much
 * harder thing to get right and a much worse thing to get wrong in a child's
 * hands. Here solvability is a property of the data, checked once.
 *
 * Prim's gives a PERFECT maze - exactly one route between any two cells -
 * with many short branches off it, which is what produces the "twisty single
 * path with lots of dead ends" the older tiers want.
 *
 * THE DIFFICULTY LADDER, re-authored because Baby was structurally a Kid
 * maze - same Prim's skeleton with the holes removed, 6-9 dead ends, every
 * corridor one cell wide. A five-year-old's first game should not be a
 * puzzle. Every figure below is measured by the offline validator, not
 * estimated, and asserted before the templates are pasted in:
 *
 *     tier    route   dead ends   holes   corridors
 *     Baby    26-30       0         0     ALL two cells wide
 *     Kid       30        5         0     one cell
 *     Teen      30       10         6     one cell
 *     Adult     34      11-15      11     one cell
 *
 * Baby is a wide, branchless path - a serpentine, an L, twin columns, a
 * single-turn spiral. A serpentine "reads as a room rather than a maze",
 * which was a fault at Kid and up and is exactly the point here: there is
 * one obvious way to go and 56 px of corridor to do it in, against a 20 px
 * Visitor. All four Baby variants are branchless and two-wide, so they are
 * equally easy; their routes are held to a 26-30 band so none is a slog.
 *
 * Kid and Teen were thinned by removing whole dead-end BRANCHES from the
 * Prim's skeletons - never a cell on the solution route, and never a branch
 * containing a hazard, so route length and hole count are untouched. Kid had
 * 12-16 dead ends, which is not "a few". Teen keeps twice Kid's branching
 * plus hazards; Adult is unchanged and remains the hardest.
 *
 * Within a tier every variant shares its route length and (for Kid and Teen)
 * its exact dead-end count, so a repeat play changes the SHAPE and nothing
 * else. Difficulty never drifts with repeated plays.
 *
 * Hazards sit only in dead-end branches, never on the route. In a
 * one-cell-wide passage with a 20 px Visitor and 28 px cells there is no room
 * to steer around a hole - on the route it would be an unavoidable reset
 * rather than a risk worth taking. Baby and Kid have none at all.
 *
 * Movement runs on its own ~30 fps timer and moves ONE small object, so the
 * dirty rectangle is the ball, not the screen. The 40 MHz display bus is not
 * touched - the frozen value stands.
 * ======================================================================= */

/* DENSER GRID: 28 px cells instead of 32 fit a 13x15 maze rather than 11x14,
 * which is 42 cells against 30 - about 40% more maze on the same panel, so
 * routes are longer and there is room for more branching. The Visitor shrank
 * to 20 px to keep the same 4 px clearance either side in a one-cell
 * corridor, so the handling feel is unchanged. */
#define MZ_W 13
#define MZ_H 15
#define MZ_CELL 28
#define MZ_X0 2
#define MZ_Y0 14
#define MZ_BALL 20     /* the Visitor itself; clears a 28 px corridor with
                        * 4 px each side, same as before at 24/32. */
#define MZ_FPS_MS 33

/* '#' wall  '.' open  'S' start  'E' exit  'O' hole */
#define MZ_VARIANTS 4

static const char *MZ_TPL[4][MZ_VARIANTS][MZ_H] = {
    {   /* BABY */
        { "#############", "#S..........#", "#...........#", "##########..#", "##########..#", "##########..#", "#...........#", "#...........#", "#..##########", "#..##########", "#..##########", "#..##########", "#..##########", "#E.##########", "#############" },
        { "#############", "#S..#####.E.#", "#...#####...#", "#...#####...#", "#...#####...#", "#...#####...#", "#...#####...#", "#...#####...#", "#...#####...#", "#...#####...#", "#...........#", "#...........#", "#...........#", "#...........#", "#############" },
        { "#############", "#S.##E.######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#..##..######", "#......######", "#......######", "#############" },
        { "#############", "#S..........#", "#...........#", "##########..#", "##########..#", "##########..#", "##########..#", "##########..#", "##########..#", "##########..#", "##########..#", "##########..#", "###E........#", "###.........#", "#############" },
    },
    {   /* KID */
        { "#############", "#S..........#", "#####.#####.#", "#####...###.#", "#######.###.#", "#####...###.#", "#####.#####.#", "#####.#.....#", "#####.###.#.#", "###...#...#.#", "#######.#####", "#######.....#", "###########.#", "###########E#", "#############" },
        { "#############", "#S..........#", "###.###.###.#", "#...#...###.#", "#####.#######", "#####.#######", "#####.#######", "#####...#####", "#######.#####", "###.....#####", "###.#.#######", "###.#.#######", "###.#.#######", "###.#......E#", "#############" },
        { "#############", "#S..#########", "###.#########", "###.....#####", "#######.#####", "###.......###", "###.#####.###", "###.###.#.###", "###.###.#####", "###.......###", "#######.#####", "#######.#####", "#######.#####", "#######....E#", "#############" },
        { "#############", "#S..#########", "###.#########", "###...#.....#", "###.#.#.#.###", "#...#...#...#", "###########.#", "#########...#", "#########.#.#", "#########.#.#", "#########.###", "#########...#", "###########.#", "###########E#", "#############" },
    },
    {   /* TEEN */
        { "#############", "#S..........#", "#.###.###.#.#", "#.###...#.#.#", "#.#####.#.#.#", "#.###.O.#.#.#", "#O###.#####.#", "#...#.#.....#", "#.###.###.#O#", "#O#...#...#.#", "#.#####.#####", "#.#O#.......#", "###.###.###.#", "###......O#E#", "#############" },
        { "#############", "#S......OO..#", "#######.###.#", "#####...#.#.#", "#####.#.#.###", "#...O.#.#...#", "#.#.#.###O#.#", "#.#.#.....#.#", "#.#####.###.#", "#.#O....#.#.#", "###.#.#.#.#.#", "###O#.#...#.#", "###.#.#######", "###.#......E#", "#############" },
        { "#############", "#S..#########", "###.#########", "###.....#...#", "#######.#.#.#", "###......O#.#", "###.#######O#", "#...###.###O#", "###.###.###.#", "#.........#.#", "#####.#.#####", "#O....#...#.#", "#####.#.###.#", "#O..O.#....E#", "#############" },
        { "#############", "#S..###.#####", "###.###O#####", "###...#...###", "###.#.#.#.###", "#.O.#...#...#", "###.#.#####.#", "#OO.#.###...#", "#.#.#.###.###", "#O#.#.###.###", "#.###.###.###", "#.#.....#...#", "#.#.#O###.#.#", "#.#.#...#.#E#", "#############" },
    },
    {   /* ADULT */
        { "#############", "#S#.........#", "#.#.#.###.#.#", "#...#.#O#O#O#", "#.#.#.#.#####", "#O#.#.......#", "#.#.###.#.###", "#.#.#...#...#", "#O#.#.#######", "#.#.#....O#.#", "#######.###.#", "#OOOO....O..#", "#.#.#.#######", "#.#.#......E#", "#############" },
        { "#############", "#S........#O#", "###.#.#####.#", "#OO.#.......#", "#.#.#####O#.#", "#.#.#.....#.#", "#.#########.#", "#...#.......#", "#O###.#######", "#O..#...#.#.#", "#######.#.#.#", "#..O.O....#.#", "#.#.###.###O#", "#O#.O.#....E#", "#############" },
        { "#############", "#S....O.#O#.#", "###.#####O#.#", "#O..#...#...#", "###.#.###.#.#", "#...O....O#O#", "###.#.#######", "#...#OO.#...#", "#.#######.#.#", "#.#.......#.#", "#.#.#O###.###", "#...#.#...#.#", "#O#.#.#.###.#", "#.#.#.#....E#", "#############" },
        { "#############", "#S#.........#", "#.#.###.###O#", "#...#.....#O#", "#.#.#.#.#####", "#.#.#.#.#.#.#", "###O#.###.#O#", "#.O.#.......#", "#.#####.###.#", "#O.O..#.#...#", "#O#.#####.###", "#.#...#.O...#", "###.#.###.#.#", "#...#.O.#O#E#", "#############" },
    },
};

static char       s_mz[MZ_H][MZ_W + 1];
static lv_timer_t *s_mz_timer;
static float      s_mz_x, s_mz_y, s_mz_vx, s_mz_vy;
static uint32_t   s_mz_t0;
static uint8_t    s_mz_sx, s_mz_sy;

static bool mz_wall(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= MZ_W || cy >= MZ_H) return true;
    return s_mz[cy][cx] == '#';
}

static char mz_at(float px, float py)
{
    const int cx = (int)((px - MZ_X0) / MZ_CELL);
    const int cy = (int)((py - MZ_Y0) / MZ_CELL);
    if (cx < 0 || cy < 0 || cx >= MZ_W || cy >= MZ_H) return '#';
    return s_mz[cy][cx];
}

static void mz_reset_ball(void)
{
    s_mz_x = MZ_X0 + s_mz_sx * MZ_CELL + (MZ_CELL - MZ_BALL) / 2.0f;
    s_mz_y = MZ_Y0 + s_mz_sy * MZ_CELL + (MZ_CELL - MZ_BALL) / 2.0f;
    s_mz_vx = s_mz_vy = 0.0f;
    lv_obj_set_pos(s_mz_ball, (lv_coord_t)s_mz_x, (lv_coord_t)s_mz_y);
}

/* --- COLLISION [regression fix] -----------------------------------------
 *
 * The previous version moved the whole frame's distance in one go, tested
 * ONE line of the Visitor's box per axis, and snapped back to the old
 * position when that test failed. Three faults, and they compounded:
 *
 *   1. OFF BY ONE. It tested `nx + MZ_BALL`, which is the pixel just PAST
 *      the right edge - so with 4 px of clearance in a 28 px corridor the
 *      Visitor was blocked while a pixel of gap remained. That alone reads
 *      as sticking.
 *   2. CENTRE-LINE ONLY. The X test sampled the vertical centre and the Y
 *      test the horizontal centre, so neither saw a CORNER. A 20 px box in
 *      28 px cells straddles two rows most of the time, so a corner could
 *      slip into a wall unnoticed.
 *   3. NO WAY OUT. Once even slightly embedded, the test asked only "is the
 *      new position blocked?" - never "is this move an improvement?" - so
 *      every candidate position was blocked and the Visitor was trapped for
 *      good. That is the corner trap.
 *
 * The fix keeps the axis separation, which was right and is what lets a
 * blocked X still slide along Y:
 *
 *   - mz_blocked() is a real box test over every cell the Visitor overlaps,
 *     using inclusive edges, so corners are seen;
 *   - movement is SUB-STEPPED a pixel at a time and stops at the last free
 *     position, so a fast tilt cannot tunnel or embed, and the Visitor rests
 *     flush against the wall instead of snapping back;
 *   - if a frame ever begins already overlapping, movement is allowed
 *     unconditionally for that frame, so nothing can be trapped permanently.
 *     Sub-stepping means this should never fire; it logs if it does. */

/* Every cell the Visitor's box overlaps at (x, y). floorf, not a cast: a
 * cast truncates toward zero, which is the wrong direction left of / above
 * the grid origin and would read an out-of-bounds box as in-bounds. */
static bool mz_blocked(float x, float y)
{
    const int cx0 = (int)floorf((x - MZ_X0) / MZ_CELL);
    const int cx1 = (int)floorf((x + MZ_BALL - 1 - MZ_X0) / MZ_CELL);
    const int cy0 = (int)floorf((y - MZ_Y0) / MZ_CELL);
    const int cy1 = (int)floorf((y + MZ_BALL - 1 - MZ_Y0) / MZ_CELL);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
            if (mz_wall(cx, cy)) return true;
    return false;
}

/* Slide one axis as far as it will go, a pixel at a time. `vel` is zeroed on
 * contact so a held tilt does not keep pressing into the wall - the next
 * frame recomputes it from the live tilt, which is what makes "release or
 * change the tilt and you move away normally" true. */
static float mz_slide(float pos, float other, float *vel, bool horizontal)
{
    const float d = *vel;
    if (d == 0.0f) return pos;
    const float dir = (d > 0.0f) ? 1.0f : -1.0f;
    float moved = 0.0f, p = pos;

    while (fabsf(moved) < fabsf(d)) {
        float s = dir;                       /* one pixel */
        if (fabsf(d - moved) < 1.0f) s = d - moved;   /* the last fraction */
        const float np = p + s;
        if (horizontal ? mz_blocked(np, other) : mz_blocked(other, np)) {
            *vel = 0.0f;
            break;
        }
        p = np;
        moved += s;
    }
    return p;
}

/* Axis-separated: move X, then move Y from wherever X ended up. A wall on
 * one axis therefore never blocks the other, which is what stops corners
 * trapping the player. */
static void mz_step(lv_timer_t *t)
{
    (void)t;
    float gx, gy, gz;
    if (!diag_imu_read_screen(&gx, &gy, &gz)) return;

    /* DISPLAY-FRAME ADAPTER - the frozen BSP mapping is NOT touched.
     *
     * board_pins.h defines the verified IMU axes against a "screen" frame
     * captured in Phase 1 by holding the board in guided poses. That capture
     * is internally consistent and stays frozen. What it does NOT establish
     * is how that frame relates to the way the CO5300 panel is actually
     * scanned out, and those two differ by a quarter turn.
     *
     * Observed on hardware, all four directions consistent:
     *   tilt down  -> ball moved right      tilt up   -> ball moved left
     *   tilt right -> ball moved down       tilt left -> ball moved up
     *
     * The axis reported as X drives VERTICAL motion on this panel, and the
     * axis reported as Y drives HORIZONTAL. So the transform is a swap plus
     * a negation on the vertical component - not a pure rotation.
     *
     * CORRECTION: I first shipped this as a swap with no sign change, having
     * inferred a clean 90-degree rotation. Hardware said otherwise - with the
     * top edge tilted DOWN the ball fell toward the BOTTOM of the screen,
     * when gravity should carry it toward the lowered top edge. The vertical
     * axis needed negating as well. Recorded because the tidy explanation
     * was the wrong one, and the next person will be tempted by it too.
     *
     * Phase 10's tilt/personality work will hit exactly the same thing; the
     * adapter belongs in one named place rather than being rediscovered. */
    const float tilt_right =  gy;   /* display +X (right) */
    const float tilt_down  = -gx;   /* display +Y (down)  */

    s_mz_vx = s_mz_vx * 0.86f + tilt_right * 2.6f;
    s_mz_vy = s_mz_vy * 0.86f + tilt_down  * 2.6f;
    if (s_mz_vx > 6.0f) s_mz_vx = 6.0f;
    if (s_mz_vx < -6.0f) s_mz_vx = -6.0f;
    if (s_mz_vy > 6.0f) s_mz_vy = 6.0f;
    if (s_mz_vy < -6.0f) s_mz_vy = -6.0f;

    const float r = MZ_BALL / 2.0f;

    if (mz_blocked(s_mz_x, s_mz_y)) {
        /* Should be unreachable: sub-stepping never leaves the Visitor
         * inside a wall, and it starts at a cell centre. If it ever happens
         * anyway, moving freely for one frame is the guaranteed way out -
         * being briefly wrong is recoverable, being stuck is not. */
        Serial.println("MAZE: recovered from an overlapping start position");
        s_mz_x += s_mz_vx;
        s_mz_y += s_mz_vy;
    } else {
        s_mz_x = mz_slide(s_mz_x, s_mz_y, &s_mz_vx, true);
        s_mz_y = mz_slide(s_mz_y, s_mz_x, &s_mz_vy, false);
    }

    lv_obj_set_pos(s_mz_ball, (lv_coord_t)s_mz_x, (lv_coord_t)s_mz_y);

    const char c = mz_at(s_mz_x + r, s_mz_y + r);
    if (c == 'O') {
        games_sfx(SFX_LOSE);
        lv_label_set_text(s_mz_info, "Oops! Back to the start.");
        mz_reset_ball();
        return;
    }
    if (c == 'E') {
        const uint32_t ms = millis() - s_mz_t0;
        games_sfx(SFX_WIN);
        /* Faster is better, floored so finishing always pays. */
        float h = 20.0f - (float)ms / 3000.0f;
        if (h < 6.0f) h = 6.0f;
        if (h > 20.0f) h = 20.0f;
        if (s_mz_timer) { lv_timer_del(s_mz_timer); s_mz_timer = nullptr; }
        finish_game((uint16_t)(ms / 100), ms, h);
    }
}

/* TEST: put the Visitor on the exit and let the REAL win path run.
 *
 * Only the tilting is substituted. The next mz_step() tick reads 'E' at the
 * Visitor's centre exactly as it would after a genuine run, so finish_game(),
 * the record write and the NVS save are all the shipping ones - which is the
 * whole point, since "does the best time persist" is a question about that
 * path and not about the accelerometer. */
void games_maze_warp_to_exit(void)
{
    if (!s_mz_timer) { Serial.println("no maze running"); return; }
    for (int y = 0; y < MZ_H; y++)
        for (int x = 0; x < MZ_W; x++)
            if (s_mz[y][x] == 'E') {
                s_mz_x = MZ_X0 + x * MZ_CELL + (MZ_CELL - MZ_BALL) / 2.0f;
                s_mz_y = MZ_Y0 + y * MZ_CELL + (MZ_CELL - MZ_BALL) / 2.0f;
                s_mz_vx = s_mz_vy = 0.0f;
                Serial.println("(test) placed on the exit - the real win path takes over");
                return;
            }
    Serial.println("no exit in this maze");
}

/* --- COLLISION SWEEP [test] ----------------------------------------------
 * Drives the SHIPPED mz_slide()/mz_blocked() at maximum speed into every
 * wall and corner of all sixteen mazes and checks the three properties the
 * regression fix has to guarantee. It exists because the alternative was
 * tilting the board by hand and concluding "felt fine", and because a test
 * that reimplements the collision maths would prove nothing about the code
 * that ships.
 *
 *   1. NEVER EMBEDDED - after any move the box overlaps no wall, however
 *      fast it was travelling. Covers tunnelling.
 *   2. NEVER TRAPPED - drive hard into a wall, then reverse; the Visitor
 *      must come away. Covers corners and "release the tilt and you move".
 *   3. ONE AXIS BLOCKED STILL SLIDES ON THE OTHER - the property that makes
 *      a corner passable rather than a trap.
 * ======================================================================= */
void games_maze_collision_sweep(void)
{
    const float V = 6.0f;          /* the velocity clamp in mz_step() */
    uint32_t placed = 0, embedded = 0, trapped = 0, no_slide = 0;

    Serial.println();
    Serial.println("=== MAZE COLLISION SWEEP ==================================");

    for (uint8_t st = 0; st < 4; st++) {
        for (uint8_t v = 0; v < MZ_VARIANTS; v++) {
            const char **tpl = MZ_TPL[st][v];
            for (int y = 0; y < MZ_H; y++) {
                for (int x = 0; x < MZ_W; x++) s_mz[y][x] = tpl[y][x];
                s_mz[y][MZ_W] = 0;
            }

            for (int cy = 0; cy < MZ_H; cy++) {
                for (int cx = 0; cx < MZ_W; cx++) {
                    if (s_mz[cy][cx] == '#') continue;
                    const float ox = MZ_X0 + cx * MZ_CELL + (MZ_CELL - MZ_BALL) / 2.0f;
                    const float oy = MZ_Y0 + cy * MZ_CELL + (MZ_CELL - MZ_BALL) / 2.0f;
                    if (mz_blocked(ox, oy)) continue;     /* cell too tight */

                    static const int DX[8] = { 1,-1, 0, 0, 1, 1,-1,-1 };
                    static const int DY[8] = { 0, 0, 1,-1, 1,-1, 1,-1 };
                    for (uint8_t d = 0; d < 8; d++) {
                        float px = ox, py = oy;
                        placed++;

                        /* 1. slam into the wall for 40 frames */
                        for (uint8_t f = 0; f < 40; f++) {
                            float vx = DX[d] * V, vy = DY[d] * V;
                            px = mz_slide(px, py, &vx, true);
                            py = mz_slide(py, px, &vy, false);
                            if (mz_blocked(px, py)) { embedded++; break; }
                        }
                        const float hx = px, hy = py;

                        /* 3. a blocked axis must not block the other one */
                        if (DX[d] && DY[d]) {
                            float tx = hx, ty = hy;
                            float vx = -DX[d] * V, vy = 0.0f;
                            tx = mz_slide(tx, ty, &vx, true);
                            vy = -DY[d] * V;
                            ty = mz_slide(ty, tx, &vy, false);
                            if (fabsf(tx - hx) < 0.5f && fabsf(ty - hy) < 0.5f)
                                no_slide++;
                        }

                        /* 2. reverse out - it must come away */
                        for (uint8_t f = 0; f < 20; f++) {
                            float vx = -DX[d] * V, vy = -DY[d] * V;
                            px = mz_slide(px, py, &vx, true);
                            py = mz_slide(py, px, &vy, false);
                        }
                        if (fabsf(px - hx) < 2.0f && fabsf(py - hy) < 2.0f) trapped++;
                    }
                }
            }
        }
    }

    Serial.printf("  %lu launches from every open cell x 8 directions, 16 mazes\n",
                  (unsigned long)placed);
    Serial.printf("  embedded in a wall : %lu   (must be 0)\n", (unsigned long)embedded);
    Serial.printf("  trapped, could not reverse out : %lu   (must be 0)\n",
                  (unsigned long)trapped);
    Serial.printf("  diagonal hit that could slide on NEITHER axis : %lu\n",
                  (unsigned long)no_slide);
    Serial.println("  (the last one is >0 only for true pockets, where both");
    Serial.println("   neighbours really are wall - that is a corner, not a trap)");
    Serial.printf("  VERDICT: %s\n",
                  (embedded == 0 && trapped == 0) ? "PASS" : "*** FAIL ***");
    Serial.println("-----------------------------------------------------------");
}

static void maze_start(void)
{
    body_reset();
    const uint8_t st = gstage();

    /* A DIFFERENT MAZE EVERY PLAY, AT THE SAME DIFFICULTY.
     *
     * Four authored templates per stage, times four transforms (identity,
     * mirror-X, mirror-Y, both) = 16 distinct mazes per tier. Every one of
     * the 64 was verified offline for a route AND a hole-free route, so
     * picking at random can never hand out an unsolvable maze.
     *
     * The templates within a stage share a route length - all four Adult
     * mazes are 34 cells, all four Kid and Teen mazes 30, all four Baby
     * mazes 26-30 - so a repeat play changes the SHAPE without changing how
     * hard it is, in either direction. Mirroring also
     * moves the start and exit corners, which is what stops a familiar
     * template being recognisable at a glance. */
    const uint8_t v  = (uint8_t)random(0, MZ_VARIANTS);
    const uint8_t tf = (uint8_t)random(0, 4);      /* bit0 = flip X, bit1 = flip Y */
    const char **tpl = MZ_TPL[st][v];

    for (int y = 0; y < MZ_H; y++) {
        for (int x = 0; x < MZ_W; x++) {
            const int sy = (tf & 2) ? (MZ_H - 1 - y) : y;
            const int sx = (tf & 1) ? (MZ_W - 1 - x) : x;
            s_mz[y][x] = tpl[sy][sx];
        }
        s_mz[y][MZ_W] = 0;
    }
    Serial.printf("MAZE: stage %u, template %u, transform %u\n", st, v, tf);

    for (int y = 0; y < MZ_H; y++)
        for (int x = 0; x < MZ_W; x++) {
            if (s_mz[y][x] == 'S') { s_mz_sx = x; s_mz_sy = y; }
            if (s_mz[y][x] == '#') {
                lv_obj_t *w = lv_obj_create(s_body);
                lv_obj_remove_style_all(w);
                lv_obj_set_size(w, MZ_CELL, MZ_CELL);
                lv_obj_set_pos(w, MZ_X0 + x * MZ_CELL, MZ_Y0 + y * MZ_CELL);
                lv_obj_set_style_bg_color(w, lv_color_hex(0x2A2A44), 0);
                lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
                lv_obj_clear_flag(w, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            } else if (s_mz[y][x] == 'O' || s_mz[y][x] == 'E') {
                lv_obj_t *w = lv_obj_create(s_body);
                lv_obj_remove_style_all(w);
                lv_obj_set_size(w, 22, 22);
                lv_obj_set_pos(w, MZ_X0 + x * MZ_CELL + 5, MZ_Y0 + y * MZ_CELL + 5);
                lv_obj_set_style_radius(w, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(w,
                    lv_color_hex(s_mz[y][x] == 'O' ? 0x101018 : 0x60D0A0), 0);
                lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
                if (s_mz[y][x] == 'O') {
                    lv_obj_set_style_border_width(w, 2, 0);
                    lv_obj_set_style_border_color(w, lv_color_hex(0x4A4A60), 0);
                }
                lv_obj_clear_flag(w, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            }
        }

    /* The Visitor rolls the maze itself rather than a generic ball. */
    s_mz_ball = mini_pet(s_body, MZ_BALL);
    lv_obj_clear_flag(s_mz_ball, LV_OBJ_FLAG_CLICKABLE);

    s_mz_info = lv_label_create(s_body);
    lv_label_set_text(s_mz_info, "Tilt to the green dot!");
    lv_obj_set_style_text_color(s_mz_info, lv_color_hex(0xB0B8C8), 0);
    lv_obj_align(s_mz_info, LV_ALIGN_BOTTOM_MID, 0, -2);

    /* Always a way out that does not need the maze to be finished. */
    g_btn(s_body, "Quit", 250, 4, 100, 36, 0x2A2A34, 0xB0B8C8, exit_cb, NULL);

    mz_reset_ball();
    s_mz_t0 = millis();
    if (s_mz_timer) lv_timer_del(s_mz_timer);
    s_mz_timer = lv_timer_create(mz_step, MZ_FPS_MS, NULL);
}

static void games_cleanup_timers(void)
{
    if (s_hl_timer)  { lv_timer_del(s_hl_timer);  s_hl_timer = nullptr; }
    if (s_rx_timer)  { lv_timer_del(s_rx_timer);  s_rx_timer = nullptr; }
    if (s_rx_clock)  { lv_timer_del(s_rx_clock);  s_rx_clock = nullptr; }
    if (s_mem_timer) { lv_timer_del(s_mem_timer); s_mem_timer = nullptr; }
    if (s_mz_timer)  { lv_timer_del(s_mz_timer);  s_mz_timer = nullptr; }
}
