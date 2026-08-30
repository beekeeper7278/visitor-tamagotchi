#pragma once
/* ===========================================================================
 * discipline - contextual telling-off  [MILESTONE 8]
 *
 * Discipline is NOT a meter you raise by pressing a button. It responds to a
 * real misbehaviour window, and pressing it when the Visitor has done nothing
 * wrong is itself a mistake with a cost.
 *
 * The line that matters: MISCHIEF is deliberate and correctable. A bathroom
 * accident caused by an ignored need, refusing food because it is genuinely
 * full, being hungry, tired or dirty - none of those are the Visitor's fault
 * and none of them open a window. Telling a child's pet off for needing the
 * toilet would be teaching the wrong lesson.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MIS_NONE = 0,
    MIS_THREW_FOOD,     /* flung food it was perfectly able to eat        */
    MIS_BEDTIME,        /* refused to settle, for fun                     */
    MIS_MESS,           /* made a mess on purpose                         */
    MIS_CAKE_TANTRUM,   /* demanded cake, loudly                          */
    MIS_COUNT
} mischief_t;

void discipline_init(void);
void discipline_tick(void);        /* 1 s: window expiry + spontaneous rolls */

/* Open a window because the Visitor genuinely misbehaved. */
void discipline_misbehave(mischief_t what);

bool        discipline_window_open(void);
mischief_t  discipline_current(void);
const char *discipline_what(mischief_t m);

/* The Care page button. Handles both the fair and the unfair case. */
void discipline_press(void);

void discipline_report(void);

#ifdef __cplusplus
}
#endif
