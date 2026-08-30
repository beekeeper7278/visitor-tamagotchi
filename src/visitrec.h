#pragma once
/* ===========================================================================
 * visitrec - permanent Visit Records  [MILESTONE 9]
 *
 * SIZING, measured rather than guessed. My first hand-count said 248 and the
 * static_assert in visitrec.cpp rejected it - the figure was 233, and the
 * pacing pass grew it to 327:
 *   fixed fields         = 53 bytes  (was 53; + care_band + length_band = 55)
 *   farewell text        = 272 bytes (was 180 - see FAREWELL_MAX below)
 *   sizeof(visit_rec_t)  = 327 bytes (asserted at compile time)
 *   VISIT_KEEP           = 8 records
 *   blob on NVS          = 8 x 327 + 8 header = 2624 bytes
 *   nvs partition        = 0x5000 = 20,480 bytes
 *   usable after NVS keeps one page for compaction  ~= 16 KB
 *   this blob            ~= 16% of usable
 *   live save (450) + game records (40) + this (2624) ~= 19% total
 *
 * FAREWELL_MAX WAS TOO SMALL AND ALWAYS HAD BEEN. At 180 the longest
 * reachable combination of opener + praise + memory + improvement + sign-off
 * measured 262 characters, so the worst notes were being silently truncated
 * mid-sentence - in the one artefact of the whole visit a child keeps. 272
 * clears the measured worst case with room for the held-departure clause,
 * and farewell_compose() now logs if it ever truncates anyway.
 *
 * Eight records USED to be "roughly half a year" against a fixed 21-day
 * visit. Visits are now 9-16 days, so eight records is about three months.
 * The value stays at 8: three months is still long enough that a child meets
 * a Visitor they can genuinely remember, and raising it trades NVS headroom
 * for history nobody has asked for yet. Oldest-first eviction beyond that.
 *
 * Its own versioned key, NOT the live save: the pet blob is written every
 * few minutes and these must never be at risk from that write path.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BUMPED 1 -> 2 by the pacing pass. visit_rec_t grew from 233 to 327 bytes
 * (longer farewell buffer, plus care_band and length_band), so v1 records
 * cannot be read as v2. There is no migration: the fields that grew sit in
 * the MIDDLE of the record, not the tail, so a prefix copy would misread
 * every byte after them. Existing history is discarded once, deliberately,
 * on the first boot of this build - and the version bump says so out loud
 * instead of leaving it to the size check to notice. */
#define VISITREC_VERSION 2
#define VISIT_KEEP       8
#define FAREWELL_MAX     272

typedef struct __attribute__((packed)) {
    uint8_t  used;
    uint8_t  final_form;
    uint8_t  trait_a, trait_b;
    uint32_t arrived_ts, departed_ts;
    uint16_t days;
    uint8_t  evo_path[4];           /* form at baby/kid/teen/adult        */
    uint8_t  fav_food, fav_game;
    uint16_t game_best[4];
    uint32_t maze_best_ms;
    uint16_t total_games;
    /* broad care summary, 0..100 each - enough to describe the visit
     * without storing the whole history */
    uint8_t  care_happy, care_fed, care_clean, care_sleep, care_disc;
    uint16_t meals, cakes, accidents, lights_forgotten, times_dirty;
    uint16_t disc_correct, disc_ignored;
    /* How the stay went, in the two terms the Journal and the note both use:
     * a broad care band, and where inside the POSSIBLE 9-16 window this visit
     * actually landed. Storing the band rather than recomputing it means a
     * record still reads correctly if the curve is ever retuned. */
    uint8_t  care_band;             /* VISIT_CARE_* */
    uint8_t  length_band;           /* VISIT_LEN_*  */
    char     farewell[FAREWELL_MAX];
} visit_rec_t;

/* Broad care band, from the same stay score that sets the departure date. */
enum { VISIT_CARE_POOR = 0, VISIT_CARE_AVERAGE, VISIT_CARE_GOOD,
       VISIT_CARE_EXCELLENT };

/* Where the stay landed inside [VISIT_DEPART_MIN_DAY, VISIT_DEPART_MAX_DAY],
 * by thirds. This is about the RANGE, not about blame: a short stay is a
 * consequence of the experience, never an accusation. */
enum { VISIT_LEN_SHORT = 0, VISIT_LEN_MIDDLE, VISIT_LEN_LONG };

const char *visitrec_care_band_name(uint8_t b);
const char *visitrec_length_band_name(uint8_t b);

void visitrec_begin(void);
uint8_t visitrec_count(void);
const visit_rec_t *visitrec_at(uint8_t idx);    /* 0 = most recent */

/* Build a record from the current Visitor and archive it, evicting the
 * oldest if full. The farewell text is stored verbatim so it reads back
 * exactly as it was shown. */
bool visitrec_archive(const char *farewell);

/* One short line a NEW Visitor can say about a previous one. NULL when
 * there is nothing worth saying. */
const char *visitrec_previous_reference(void);

void visitrec_report(void);
void visitrec_clear_all(void);      /* deliberate history wipe only */

#ifdef __cplusplus
}
#endif
