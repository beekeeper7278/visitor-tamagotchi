#pragma once
/* ===========================================================================
 * farewell - end of visit  [MILESTONE 9]
 *
 * The note is built LOCALLY from deterministic templates plus the real visit
 * history. No network, no model - it must work on a device in a drawer.
 *
 * Tone rule, and it is the hard one: warm, funny, never guilt-heavy. A child
 * who left the Visitor filthy for a week should still want to read this. The
 * "could improve" clause is included ONLY when genuinely warranted, and is
 * phrased as a suggestion between friends rather than a report card.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compose the note from the current Visitor's history. Deterministic for a
 * given history. */
void farewell_compose(char *out, size_t len);

void farewell_begin(void);        /* enter the farewell state + screen */
bool farewell_active(void);

/* Has the visit reached its end? Checked once per sim tick. */
bool farewell_due(void);

/* Test hook: acknowledge without a tap. Same path as the button. */
void farewell_acknowledge(void);

#ifdef __cplusplus
}
#endif
