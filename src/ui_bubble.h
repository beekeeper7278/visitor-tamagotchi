#pragma once
/* ===========================================================================
 * ui_bubble - speech bubble manager  [SPEC section 10]
 *
 * Four tiers, one bubble on screen at a time, higher tier preempts with a
 * 150 ms fade. Duration 2.5 s + 40 ms/char capped at 5 s. Global cooldown
 * 8 s; per-tier T0 20 s / T1 3 s / T2 45 s / T3 90 s. No string repeat
 * within the last 5 bubbles. All bubbles suppressed while a menu page or
 * game is open.
 *
 * THE SUPPRESSION IS THE FEATURE. Most requests are expected to be refused -
 * that is what stops the pet spamming a five-year-old. ui_bubble_say()
 * therefore returns whether it was accepted AND logs the reason either way,
 * so the rules can be verified on the device instead of guessed at.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUBBLE_T0_CRITICAL = 0,   /* critical need   */
    BUBBLE_T1_REACTION,       /* reaction to input */
    BUBBLE_T2_MOOD,           /* mood flavour    */
    BUBBLE_T3_IDLE,           /* idle chatter    */
    BUBBLE_TIER_COUNT
} bubble_tier_t;

void ui_bubble_create(lv_obj_t *parent);

/* Returns true if the bubble was shown. Always prints one line explaining
 * the decision. */
bool ui_bubble_say(bubble_tier_t tier, const char *text);

/* TEST ONLY. Forces a bubble on screen bypassing priority and cooldown, and
 * prints the resulting geometry with an explicit in-bounds verdict, so the
 * layout can be checked numerically instead of by eye. Deliberately does NOT
 * touch any cooldown state, so running it cannot disturb the policy that the
 * S stress test measures. */
void ui_bubble_test_show(const char *text);

/* Suppress everything - menus and games set this from Phase 3 onward. */
void ui_bubble_set_suppressed(bool s);
bool ui_bubble_suppressed(void);

/* Driven from t_anim. Handles expiry and repositioning over the pet. */
void ui_bubble_tick(void);

bool ui_bubble_visible(void);
void ui_bubble_stats(uint32_t *accepted, uint32_t *suppressed);
void ui_bubble_reset_stats(void);
const char *ui_bubble_tier_name(bubble_tier_t t);

#ifdef __cplusplus
}
#endif
