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

/* SAY IT, OR HOLD IT UNTIL IT CAN BE SEEN. [PHASE 9.5]
 *
 * ui_bubble_say() refuses everything while a menu or a game is open, which
 * is right for chatter and wrong for a REACTION - the player switched the
 * lights off from the Care page and is supposed to watch the Visitor
 * respond, but the response was spoken to a covered screen and its timer ran
 * out behind the menu. Anything the player is meant to SEE goes through this
 * instead: it is shown immediately when the pet screen is visible, and
 * otherwise queued and shown once the screen comes back - with its duration
 * starting THEN, not when it was queued.
 *
 * Bounded hard, because an unbounded queue of held reactions is both a
 * memory question and a worse experience: BUBBLE_DEFER_SLOTS entries, oldest
 * dropped, and anything that has waited longer than BUBBLE_DEFER_HOLD_MS is
 * discarded rather than surfacing minutes late and out of context.
 *
 * Returns true if it was spoken immediately, false if it was queued (or
 * dropped). The queue is drained from ui_bubble_tick(). */
bool    ui_bubble_say_deferred(bubble_tier_t tier, const char *text);
uint8_t ui_bubble_deferred_count(void);
void    ui_bubble_drop_deferred(void);   /* farewell / reset: forget them all */

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
