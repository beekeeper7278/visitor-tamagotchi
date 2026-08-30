#pragma once
/* ===========================================================================
 * visitrec - permanent Visit Records  [MILESTONE 9]
 *
 * SIZING, measured rather than guessed. My first hand-count said 248 and the
 * static_assert in visitrec.cpp rejected it - the real figure is 233:
 *   sizeof(visit_rec_t)  = 233 bytes  (asserted at compile time)
 *   VISIT_KEEP           = 8 records
 *   blob on NVS          = 8 x 233 + 8 header = 1872 bytes
 *   nvs partition        = 0x5000 = 20,480 bytes
 *   usable after NVS keeps one page for compaction  ~= 16 KB
 *   this blob            ~= 11% of usable
 *   live save (450) + game records (40) + this (1872) ~= 15% total
 *
 * Eight is chosen because a 21-day visit means eight records is roughly half
 * a year of history - long enough that a child meets a Visitor they can
 * genuinely remember, short enough to leave 85% of NVS free for later
 * phases. Oldest-first eviction beyond that.
 *
 * Its own versioned key, NOT the live save: the pet blob is written every
 * few minutes and these must never be at risk from that write path.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISITREC_VERSION 1
#define VISIT_KEEP       8
#define FAREWELL_MAX     180

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
    char     farewell[FAREWELL_MAX];
} visit_rec_t;

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
