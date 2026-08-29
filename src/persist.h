#pragma once
/* ===========================================================================
 * persist - maps the live Visitor onto the verified NVS layer  [MILESTONE 6]
 *
 * storage.* owns the blob, the CRC, the shadow-compare and the write floor,
 * and has been verified since Phase 1. This module owns only the mapping,
 * so the flash-wear protections stay in exactly one place.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

load_result_t persist_load(void);       /* NVS -> live state */
bool          persist_save(bool force); /* live state -> NVS */

/* Call after a transition where losing the state would be surprising - a
 * meal, an accident, a clean-up, the clock being set. Still goes through
 * storage_save()'s minimum-interval floor and shadow compare, so this is a
 * request, not a guaranteed write. */
void persist_mark_dirty(const char *reason);

/* Driven from t_save. */
void persist_tick(void);
void persist_report(void);

/* Print the live state in one comparable block: SAVED -> RAW LOADED ->
 * SIMULATED. The only way to tell a persistence bug from a simulator effect. */
void persist_print_state(const char *tag);

#ifdef __cplusplus
}
#endif
