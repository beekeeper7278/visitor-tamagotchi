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

/* --- PHASE 9.5: frequency and memory ------------------------------------
 * Two things changed, and they are separate ideas that happen to meet here.
 *
 * FREQUENCY is now STAGE-WEIGHTED: Kid most, Baby second, Teen third, Adult
 * least (MISCHIEF_W_*). Babies used to be excluded entirely, which removed
 * the whole mechanic from the first day of every single visit - the one day
 * a child is most likely to be watching. What a Baby actually does is
 * limited to the harmless kinds; see baby_safe() in the implementation.
 *
 * MEMORY is pet_state.learned_mischief, a rolling EMA over how windows were
 * RESOLVED. Corrected pulls it toward 0, ignored pulls it toward 100. It
 * feeds back into the roll, so early discipline genuinely calms a Teen and
 * early neglect genuinely does not - but because it is an EMA and not a
 * counter, nothing is ever locked in. A Visitor written off as a Kid can be
 * brought round as a Teen. On a child's device that recoverability is not a
 * nicety, it is the point.
 *
 * WHAT IS STILL NOT MISCONDUCT, unchanged and non-negotiable: hunger,
 * tiredness, dirt, a bathroom accident caused by an ignored need, and
 * refusing food when genuinely full. */

/* 0..100 learned tendency, for the Journal and the console report. */
float discipline_learned(void);

/* Hold off the next spontaneous opportunity for `ms`. Used when the Visitor
 * has just arrived and the screen belongs to something else - the hatch
 * reveal, its first words, the return greeting. Mischief the player TRIGGERS
 * is unaffected; this only paces the spontaneous roll. */
void discipline_settle(uint32_t ms);

/* The stage's share of the base rate, as a percentage. Exposed so the
 * console can PRINT the ordering rather than asking anyone to trust it. */
uint8_t discipline_stage_weight(uint8_t stage);

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
