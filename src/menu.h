#pragma once
/* ===========================================================================
 * menu - six-page horizontally looping pager  [SPEC section 4]
 *
 * Only the current page exists as LVGL objects; the neighbour is built at
 * x = +/-368 during a transition and the old one is deleted after. Index is
 * mod 6, so the 6->1 wrap costs nothing - no sentinel clones and no
 * scroll-snap, which cannot wrap and would need all six pages resident.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void menu_open(void);
void menu_close(void);
void menu_toggle(void);
bool menu_is_open(void);

void    menu_goto(uint8_t page);        /* absolute, wraps                  */
void    menu_step(int8_t dir);          /* -1 / +1, wraps                   */
uint8_t menu_page(void);

/* True while a swipe has been committed for the current press. Buttons check
 * this so a swipe that starts on a card never also fires that card. */
bool menu_swipe_active(void);

/* Gesture measurement: prints duration, travel and dx/dy for every touch so
 * the [GUESS] thresholds can be tuned from a real finger on this panel. */
void menu_set_metrics(bool on);
bool menu_metrics(void);

const char *menu_transition_name(void);
uint8_t     menu_transition(void);
void        menu_set_transition(uint8_t t);   /* PAGE_TR_* */

#ifdef __cplusplus
}
#endif
