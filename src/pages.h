#pragma once
/* ===========================================================================
 * pages - the six menu pages [SPEC section 4]
 *
 * Each builder returns a full-screen object parented to the pager container.
 * Only the current page (plus a neighbour mid-transition) ever exists.
 *
 * MILESTONE 3A builds all six layouts and makes Stats real with demo values.
 * MILESTONE 3B makes Food and Care functional. Clock, Games and Journal stay
 * laid out but inert - each is filled in by the phase that owns it, and a
 * plausible-looking fake would be worse than a visible placeholder.
 * ======================================================================== */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Page ORDER. Care sits before Games by request - a deliberate deviation
 * from the brief's section 4 ordering, which had Games third. Care is the
 * page a parent reaches for; Games is the one a child browses.
 *
 * Everything else keys off these values (page_create, page_name, the dots),
 * so reordering here reorders the pager with no other change. */
enum { PAGE_STATS = 0, PAGE_FOOD, PAGE_CARE, PAGE_GAMES, PAGE_CLOCK, PAGE_JOURNAL };

lv_obj_t   *page_create(uint8_t idx, lv_obj_t *parent);
const char *page_name(uint8_t idx);

#ifdef __cplusplus
}
#endif
