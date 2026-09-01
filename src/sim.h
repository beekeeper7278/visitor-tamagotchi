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
    bool asleep;        /* the Visitor is in bed - drives the STAT RATES   */
    bool lights_on;
    bool offline;       /* offline chunks obey the damage budget below     */

    /* THE CLOCK says this is a sleep period, which is NOT the same thing as
     * `asleep`. A scripted action can take the Visitor out of bed at 3 am,
     * and sim_catch_up() deliberately clears `asleep` at the end of a boot
     * catch-up ("you are here now") - both leave the flag false while the
     * night is still very much in progress.
     *
     * Rates follow `asleep`, because that is about the Visitor. The sleep
     * PERIOD follows this, because that is about the schedule. Conflating
     * them made a boot at 2 am close the night and open a second one, so
     * one night produced two dreams. */
    bool sleep_window;
    bool nap;           /* ...and the period is an afternoon nap           */
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

/* --- A USER-INITIATED CLOCK CORRECTION IS NOT ELAPSED TIME ---------------
 * These two live here, beside sim_catch_up(), because the whole point of
 * them is that they are its OPPOSITE and the distinction is the bug.
 *
 * sim_catch_up() answers "the clock moved forward while we were not looking,
 * so that much life happened". A parent fixing a wrong date is the other
 * thing entirely: the clock moved and NOTHING happened. Running one where
 * the other belongs is what made a three-hour-old Visitor five days old,
 * evolve twice, arrive to a week of hunger and start packing to leave.
 *
 * The repair is a REBASE, not a replay. Every timestamp this project stores
 * is an absolute reading of the clock that has just been found wrong, so all
 * of them move together by the same delta and every DURATION between them -
 * age, the hatch countdown, how long a departure has been held, the
 * repeat-play window - comes out unchanged. Nothing is simulated, no journal
 * entry is written, no need is advanced.
 *
 *   sim_clock_corrected()     the clock was trusted before AND after: there
 *                             is a real delta, so rebase by it.
 *   sim_clock_first_trusted() there was no usable "before" (an unset or
 *                             implausible RTC), so there is no delta to
 *                             rebase by. Anchor the simulation to now so the
 *                             unmeasurable gap is never charged to anyone.
 *
 * Both are for a HUMAN setting the clock. The `%` `.` `,` time-travel
 * diagnostics deliberately do the opposite - they move hatch_ts and leave
 * the clock alone - and are unchanged. */
void sim_clock_corrected(uint32_t old_now, uint32_t new_now);
void sim_clock_first_trusted(uint32_t now);

/* The single highest-priority return line, never a stack of greetings. */
const char *sim_return_greeting(const sim_report_t *r);

/* GONE, deliberately. Dream eligibility used to be decided here, from the
 * chunk counts of one absence - which meant the offline path and the live
 * path had separate rules and neither knew what the other had already done.
 * It now lives with the sleep period itself in care_advance(), so an absence
 * and a night spent at the device are accumulated by the same code and a
 * period that spans both is still ONE period. See pet.h. */

#ifdef __cplusplus
}
#endif
