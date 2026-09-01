#pragma once
/* ===========================================================================
 * journal - bounded milestone log  [MILESTONE 9]
 *
 * A ring of 24 eight-byte entries, living in the existing save_t.journal[]
 * slot reserved for it in Phase 1. Bounded on purpose: this is a keepsake,
 * not a telemetry stream. Only events a child would actually remember get
 * logged - hatching, evolving, a first game, a new record, a first accident.
 * Mundane ticks never appear.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    JM_HATCHED = 0, JM_EVOLVED, JM_FIRST_GAME, JM_RECORD, JM_FAV_FOOD,
    JM_ACCIDENT, JM_SPOTLESS, JM_FILTHY, JM_MISCHIEF, JM_DISCIPLINED,
    JM_CAKE, JM_LIGHTS, JM_COUNT
};

void journal_add(uint8_t type, uint8_t arg, uint16_t value);
uint8_t journal_count(void);

/* Rendered newest-first for the page. Returns false past the end. */
bool journal_line(uint8_t idx, char *buf, size_t len);

void journal_load(const save_t *b);
void journal_store(save_t *b);
void journal_clear(void);

/* Move every recorded date by a CLOCK CORRECTION. See sim_clock_corrected().
 *
 * These are absolute readings of the same clock that has just been found to
 * be wrong, so leaving them alone would print milestones dated days before
 * the (corrected) arrival they followed. Entries stamped 0 - logged while
 * the clock was untrusted - stay 0: "no date" is not a date to move. */
void journal_shift_ts(int32_t delta);

#ifdef __cplusplus
}
#endif
