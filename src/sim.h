#pragma once
/* ===========================================================================
 * sim - elapsed-time reconstruction  [MILESTONE 6]
 *
 * Split out from pet/care deliberately: this is the subtlest code in the
 * project, it runs in full only once per boot, and burying it inside another
 * module guarantees it never gets read carefully.
 *
 * THE CORE RULE: live ticks and offline catch-up share ONE stat-advance
 * implementation (care_advance). Two implementations of the hunger,
 * cleanliness and sleep rules would drift, and the offline one - which runs
 * rarely - would rot silently.
 *
 * TIME IS NEVER CAPPED. Age, days alive, stage timing and visit duration
 * always advance by the full elapsed interval. What is capped is how much
 * DAMAGE a single absence may do, each stat independently, so a week in a
 * drawer is a story rather than a dead pet.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the world was doing during a chunk. */
typedef struct {
    bool asleep;
    bool lights_on;
    bool offline;       /* offline chunks obey the damage budget below     */
} sim_ctx_t;

/* Remaining damage allowance for one absence. Live ticks pass a budget with
 * everything effectively unlimited. */
typedef struct {
    float   hunger_left, happy_left, clean_left;
    uint8_t accidents_left;
    bool    cap_hunger, cap_happy, cap_clean, cap_accident;
} sim_budget_t;

typedef struct {
    bool     ran;
    uint32_t elapsed_sec;
    uint32_t chunks;
    uint16_t days_before, days_after;
    float    hunger_before, hunger_after;
    float    happy_before,  happy_after;
    float    clean_before,  clean_after;
    float    energy_before, energy_after;
    float    bath_before,   bath_after;
    bool     accident;
    uint32_t sleep_chunks, nap_chunks, awake_chunks, lights_on_sleep_chunks;
    uint8_t  messes_kept;
    bool     evolved;        /* a form change happened during catch-up */
    sim_budget_t budget;
} sim_report_t;

/* Is this wall-clock hour a sleep hour for this stage? Babies also nap. */
bool sim_is_sleep_hour(uint8_t hour, uint8_t stage, bool *is_nap);

/* Reconstruct from_ts -> to_ts. Chunked so sleep and time-of-day boundaries
 * land correctly; a single multi-day dt would apply one wrong rate to the
 * whole gap. */
void sim_catch_up(uint32_t from_ts, uint32_t to_ts, sim_report_t *out);

const sim_report_t *sim_last_report(void);
void  sim_print_report(void);

/* The single highest-priority return line, never a stack of greetings. */
const char *sim_return_greeting(const sim_report_t *r);

#ifdef __cplusplus
}
#endif
