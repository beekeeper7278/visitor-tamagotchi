/* ===========================================================================
 * ui_bubble - speech bubble manager. See ui_bubble.h for the rules.
 * ======================================================================== */

#include <Arduino.h>
#include <string.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "ui_pet.h"
#include "ui_bubble.h"

static lv_obj_t *o_box, *o_label;

static bool     s_suppressed = false;
static bool     s_visible    = false;
static uint32_t s_expires_at = 0;
static int      s_active_tier = -1;

static uint32_t s_last_any    = 0;     /* start time of the last bubble     */
static bool     s_had_any     = false;
static uint32_t s_last_tier[BUBBLE_TIER_COUNT];
static bool     s_had_tier[BUBBLE_TIER_COUNT];

/* Recent strings, for the "no repeat within the last 5" rule. Pointers are
 * enough: every line is a string literal with static storage. */
static const char *s_recent[BUBBLE_NO_REPEAT_DEPTH];
static uint8_t     s_recent_n = 0;

static uint32_t s_n_accept = 0, s_n_suppress = 0;

static const uint32_t TIER_CD[BUBBLE_TIER_COUNT] = {
    BUBBLE_T0_CD_MS, BUBBLE_T1_CD_MS, BUBBLE_T2_CD_MS, BUBBLE_T3_CD_MS
};

const char *ui_bubble_tier_name(bubble_tier_t t)
{
    switch (t) {
        case BUBBLE_T0_CRITICAL: return "T0/critical";
        case BUBBLE_T1_REACTION: return "T1/reaction";
        case BUBBLE_T2_MOOD:     return "T2/mood";
        case BUBBLE_T3_IDLE:     return "T3/idle";
        default:                 return "T?";
    }
}

void ui_bubble_create(lv_obj_t *parent)
{
    o_box = lv_obj_create(parent);
    lv_obj_remove_style_all(o_box);
    lv_obj_set_style_bg_color(o_box, lv_color_hex(0xF2F4F8), 0);
    lv_obj_set_style_bg_opa(o_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o_box, 14, 0);
    lv_obj_set_style_pad_all(o_box, BUBBLE_PAD_PX, 0);
    lv_obj_set_style_border_width(o_box, BUBBLE_BORDER_PX, 0);
    /* Size is computed explicitly per message in set_text_sized(). We do NOT
     * use LV_SIZE_CONTENT: its refresh is deferred, so lv_obj_get_width()
     * during positioning returned the PREVIOUS message's width and the edge
     * clamp then ran on stale geometry. Measuring the text ourselves makes
     * the size known before we position, which is the only way the clamp can
     * be correct on the same frame. */
    lv_obj_set_style_border_color(o_box, lv_color_hex(0x2A2A34), 0);
    lv_obj_clear_flag(o_box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o_box, LV_OBJ_FLAG_HIDDEN);

    o_label = lv_label_create(o_box);
    lv_obj_set_style_text_color(o_label, lv_color_hex(0x16161E), 0);
    lv_obj_set_style_text_font(o_label, &lv_font_montserrat_20, 0);
    lv_label_set_long_mode(o_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(o_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(o_label, "");
    /* Width is NOT fixed here - set_text_sized() measures each string and
     * sizes the label per message. A fixed width was the second half of the
     * clipping bug: at 278 px nothing short of a paragraph ever wrapped. */

    memset(s_last_tier, 0, sizeof(s_last_tier));
    memset(s_had_tier, 0, sizeof(s_had_tier));
    memset(s_recent, 0, sizeof(s_recent));
}

static bool recently_said(const char *txt)
{
    for (uint8_t i = 0; i < s_recent_n; i++)
        if (s_recent[i] && strcmp(s_recent[i], txt) == 0) return true;
    return false;
}

static void remember(const char *txt)
{
    if (s_recent_n < BUBBLE_NO_REPEAT_DEPTH) {
        s_recent[s_recent_n++] = txt;
    } else {
        for (uint8_t i = 1; i < BUBBLE_NO_REPEAT_DEPTH; i++) s_recent[i - 1] = s_recent[i];
        s_recent[BUBBLE_NO_REPEAT_DEPTH - 1] = txt;
    }
}


/* Size the label to the text: natural width for short strings, clamped to
 * BUBBLE_TEXT_MAX_W for long ones so LV_LABEL_LONG_WRAP has something to
 * wrap against. The box is LV_SIZE_CONTENT, so it follows automatically and
 * its height grows with the wrapped line count. */
static lv_coord_t s_box_w = 0, s_box_h = 0;   /* known before positioning */
static int        s_lines = 0;
static lv_coord_t s_box_x = 0, s_box_y = 0;   /* what we actually set */

static void set_text_sized(const char *txt)
{
    const lv_font_t *font = lv_obj_get_style_text_font(o_label, LV_PART_MAIN);
    const lv_coord_t ls   = lv_obj_get_style_text_letter_space(o_label, LV_PART_MAIN);
    const lv_coord_t lsp  = lv_obj_get_style_text_line_space(o_label, LV_PART_MAIN);
    const lv_coord_t chrome = 2 * (BUBBLE_PAD_PX + BUBBLE_BORDER_PX);

    /* 1. how wide would this be on ONE unconstrained line? */
    lv_point_t nat;
    lv_txt_get_size(&nat, txt, font, ls, lsp, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    lv_coord_t tw = nat.x;
    if (tw > BUBBLE_TEXT_MAX_W) tw = BUBBLE_TEXT_MAX_W;
    if (tw < BUBBLE_TEXT_MIN_W) tw = BUBBLE_TEXT_MIN_W;

    /* 2. re-measure AT that width, so a wrapped string reports its real
     *    multi-line height and the box grows to fit the lines. */
    lv_point_t wrapped;
    lv_txt_get_size(&wrapped, txt, font, ls, lsp, tw, LV_TEXT_FLAG_NONE);

    s_box_w = tw + chrome;
    s_box_h = wrapped.y + chrome;
    s_lines = (font && font->line_height > 0)
            ? (wrapped.y + font->line_height / 2) / font->line_height : 0;

    lv_label_set_text(o_label, txt);
    lv_obj_set_width(o_label, tw);
    lv_obj_set_size(o_box, s_box_w, s_box_h);
    lv_obj_align(o_label, LV_ALIGN_CENTER, 0, 0);
}

/* Anchor above the pet, clamped so the box never leaves the screen. */
static void reposition(void)
{
    lv_coord_t px, py;
    ui_pet_anchor(&px, &py);
    /* s_box_w/h were computed from the text in set_text_sized(), so they are
     * correct NOW - unlike lv_obj_get_width(), which lags by one refresh. */
    const lv_coord_t w = s_box_w;
    const lv_coord_t h = s_box_h;
    if (w <= 0 || h <= 0) return;

    const lv_coord_t m = BUBBLE_SCREEN_MARGIN;
    lv_coord_t x = px - w / 2;              /* centred over the pet         */
    lv_coord_t y = py - h - BUBBLE_TAIL_GAP_PX;

    /* Clamp the WHOLE box inside the panel on all four edges. Right/bottom
     * are clamped before left/top so that if the box were ever wider than
     * the screen the visible corner is the top-left one, not a bubble that
     * has slid off to the right. */
    if (x + w > BSP_LCD_W - m) x = BSP_LCD_W - m - w;
    if (y + h > BSP_LCD_H - m) y = BSP_LCD_H - m - h;
    if (x < m) x = m;
    if (y < m) y = m;

    s_box_x = x; s_box_y = y;
    lv_obj_set_pos(o_box, x, y);
}

static void fade_to(lv_opa_t target, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o_box);
    lv_anim_set_values(&a, lv_obj_get_style_opa(o_box, 0), target);
    lv_anim_set_time(&a, ms);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
    });
    lv_anim_start(&a);
}

static void hide_now(void)
{
    lv_anim_del(o_box, NULL);
    lv_obj_add_flag(o_box, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
    s_active_tier = -1;
}

bool ui_bubble_say(bubble_tier_t tier, const char *text)
{
    if (tier >= BUBBLE_TIER_COUNT || !text || !*text) return false;
    const uint32_t now = millis();

    /* --- refusal checks, most specific first ---------------------------- */

    if (s_suppressed) {
        s_n_suppress++;
        Serial.printf("BUBBLE SUPPRESS %-11s \"%s\"  (menu/game open)\n",
                      ui_bubble_tier_name(tier), text);
        return false;
    }

    /* A lower-or-equal tier can never interrupt a bubble already on screen. */
    const bool preempting = s_visible && (int)tier < s_active_tier;
    if (s_visible && !preempting) {
        s_n_suppress++;
        Serial.printf("BUBBLE SUPPRESS %-11s \"%s\"  (%s already showing)\n",
                      ui_bubble_tier_name(tier), text,
                      ui_bubble_tier_name((bubble_tier_t)s_active_tier));
        return false;
    }

    /* Preemption deliberately bypasses the cooldowns below. A critical need
     * that arrives during an idle-chatter cooldown must still be heard -
     * otherwise T0 could be silenced by chatter, which inverts the whole
     * point of the tiers. */
    if (!preempting) {
        if (s_had_any && (now - s_last_any) < BUBBLE_GLOBAL_CD_MS) {
            s_n_suppress++;
            Serial.printf("BUBBLE SUPPRESS %-11s \"%s\"  (global cooldown, %lu ms left)\n",
                          ui_bubble_tier_name(tier), text,
                          (unsigned long)(BUBBLE_GLOBAL_CD_MS - (now - s_last_any)));
            return false;
        }
        if (s_had_tier[tier] && (now - s_last_tier[tier]) < TIER_CD[tier]) {
            s_n_suppress++;
            Serial.printf("BUBBLE SUPPRESS %-11s \"%s\"  (tier cooldown, %lu ms left)\n",
                          ui_bubble_tier_name(tier), text,
                          (unsigned long)(TIER_CD[tier] - (now - s_last_tier[tier])));
            return false;
        }
    }

    if (recently_said(text)) {
        s_n_suppress++;
        Serial.printf("BUBBLE SUPPRESS %-11s \"%s\"  (said within last %d)\n",
                      ui_bubble_tier_name(tier), text, BUBBLE_NO_REPEAT_DEPTH);
        return false;
    }

    /* --- accepted -------------------------------------------------------- */

    uint32_t dur = BUBBLE_BASE_MS + (uint32_t)strlen(text) * BUBBLE_PER_CHAR_MS;
    if (dur > BUBBLE_MAX_MS) dur = BUBBLE_MAX_MS;

    set_text_sized(text);
    lv_obj_clear_flag(o_box, LV_OBJ_FLAG_HIDDEN);
    reposition();

    if (preempting) {
        /* cross-fade rather than a hard cut, so a preempt reads as the pet
         * changing its mind instead of the screen glitching */
        lv_obj_set_style_opa(o_box, LV_OPA_TRANSP, 0);
        fade_to(LV_OPA_COVER, BUBBLE_FADE_MS);
    } else {
        lv_anim_del(o_box, NULL);
        lv_obj_set_style_opa(o_box, LV_OPA_COVER, 0);
    }

    s_visible     = true;
    s_active_tier = (int)tier;
    s_expires_at  = now + dur;
    s_last_any    = now;  s_had_any = true;
    s_last_tier[tier] = now; s_had_tier[tier] = true;
    remember(text);
    s_n_accept++;

    Serial.printf("BUBBLE ACCEPT   %-11s \"%s\"  (%lu ms%s)\n",
                  ui_bubble_tier_name(tier), text, (unsigned long)dur,
                  preempting ? ", preempted" : "");
    return true;
}

void ui_bubble_test_show(const char *text)
{
    if (!text || !*text) return;

    set_text_sized(text);
    lv_anim_del(o_box, NULL);
    lv_obj_set_style_opa(o_box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o_box, LV_OBJ_FLAG_HIDDEN);
    reposition();

    /* Keep it on screen long enough to photograph, but still expire on its
     * own so a test bubble never becomes a stuck bubble. */
    s_visible     = true;
    s_active_tier = (int)BUBBLE_T0_CRITICAL;
    s_expires_at  = millis() + BUBBLE_MAX_MS;

    /* Report the values WE computed. lv_obj_get_x()/get_width() are both
     * refreshed lazily, so querying them here reported a stale position -
     * every row looked identical regardless of string or pet position. */
    const lv_coord_t x = s_box_x, y = s_box_y;
    const lv_coord_t w = s_box_w, h = s_box_h;
    const int lines = s_lines;

    const bool ok = (x >= 0) && (y >= 0) &&
                    (x + w <= BSP_LCD_W) && (y + h <= BSP_LCD_H);

    Serial.printf("  box x=%3d y=%3d w=%3d h=%3d  lines=%d  %s  \"%s\"\n",
                  (int)x, (int)y, (int)w, (int)h, lines,
                  ok ? "IN BOUNDS" : "*** OFF SCREEN ***", text);
    if (!ok)
        Serial.printf("      FAIL: right=%d bottom=%d vs panel %dx%d\n",
                      (int)(x + w), (int)(y + h), BSP_LCD_W, BSP_LCD_H);
}

void ui_bubble_tick(void)
{
    if (!s_visible) return;
    if ((int32_t)(millis() - s_expires_at) >= 0) { hide_now(); return; }
    reposition();      /* follow the pet while it walks */
}

void ui_bubble_set_suppressed(bool s)
{
    s_suppressed = s;
    if (s && s_visible) hide_now();
}
bool ui_bubble_suppressed(void) { return s_suppressed; }
bool ui_bubble_visible(void)    { return s_visible; }

void ui_bubble_stats(uint32_t *a, uint32_t *s)
{
    if (a) *a = s_n_accept;
    if (s) *s = s_n_suppress;
}

void ui_bubble_reset_stats(void) { s_n_accept = s_n_suppress = 0; }
