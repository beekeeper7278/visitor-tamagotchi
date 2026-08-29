#pragma once
/* ===========================================================================
 * strings - every line of dialogue, one file  [SPEC section 1]
 *
 * Kept in a single file deliberately: 200+ lines of kid-facing dialogue
 * scattered across ten modules will drift in tone, and this way the whole
 * voice is reviewable in one pass.
 *
 * PHASE 2 holds sample lines only - enough to exercise all four bubble tiers
 * on hardware. The real dialogue arrives with the mechanics that motivate it
 * (food in Phase 4, sleep in Phase 5, and so on).
 * ======================================================================== */

#include "ui_bubble.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A random line for the tier. Never returns NULL. */
const char *strings_random(bubble_tier_t tier);

/* Deterministic access, so a test can walk every line in order. */
const char *strings_at(bubble_tier_t tier, uint8_t index);
uint8_t     strings_count(bubble_tier_t tier);

#ifdef __cplusplus
}
#endif
