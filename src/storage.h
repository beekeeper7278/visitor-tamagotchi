#pragma once
/* NVS persistence: schema versioning, CRC integrity, migration, safe reset.
 * The ONLY module in this project that touches NVS.
 *
 * Landed in Phase 1 deliberately. Every later phase then persists from
 * birth, so there is never a "retrofit persistence into everything" pass -
 * which is where schema bugs come from. */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_SCHEMA_VERSION 2

/* Journal entry classes - see design doc section 11 */
enum { JRN_MILESTONE = 0, JRN_RECORD = 1, JRN_FLAVOUR = 2 };

typedef struct __attribute__((packed)) {
    uint32_t ts;
    uint8_t  type;
    uint8_t  arg;
    uint16_t value;
} journal_entry_t;          /* 8 bytes */

/* Flag bits for save_t.flags */
#define SF_ASLEEP        (1u << 0)
#define SF_LIGHTS_ON     (1u << 1)
#define SF_RTC_SUSPECT   (1u << 2)
#define SF_FAREWELL_ARM  (1u << 3)
#define SF_HATCHED       (1u << 4)

typedef struct __attribute__((packed)) {
    /* --- header --- */
    uint16_t schema;
    uint16_t struct_size;
    uint32_t crc32;              /* over every byte AFTER this field */

    /* --- time --- */
    uint32_t hatch_ts;
    uint32_t last_save_ts;
    uint32_t last_sim_ts;
    uint32_t last_rtc_ts;

    /* --- live stats --- */
    float    hunger, happiness, discipline, cleanliness, energy;
    float    weight_g;

    /* --- identity --- */
    uint8_t  stage, form_id, kid_form, teen_form;
    uint16_t days_alive_max;     /* monotonic; age never goes backwards */
    uint8_t  flags;
    uint8_t  visit_index;

    /* --- hidden care accumulators --- */
    float    care_happy, care_fed, care_clean;
    float    care_sleep, care_discipline, nutrition;
    uint16_t ignored_requests, games_played, meals;
    uint16_t junk_meals, disc_correct, disc_unfair;

    /* --- world --- */
    uint8_t  mess_count, pending_need;
    uint16_t _pad0;
    uint32_t pending_need_ts, last_game_ts, last_meal_ts;

    /* --- records --- */
    uint16_t player_high[4];     /* reaction, memory, higher/lower, maze */
    uint16_t pet_high[4];
    uint32_t maze_best_ms;
    uint16_t cakes_eaten, lights_forgotten, times_dirty, total_games;
    uint8_t  food_count[3];      /* burger, fruit, cake */
    uint8_t  _pad1;

    /* --- journal --- */
    journal_entry_t journal[24];

    /* --- schema 2 additions ------------------------------------------------
     * APPENDED ON PURPOSE. v1 is then a strict byte prefix of v2, so the
     * migration is "copy the old blob, zero the tail" - which is both trivial
     * and actually testable, rather than a field-by-field remap that nobody
     * ever exercises.
     *
     * Messes are stored individually, not just as a count: a poop left before
     * shutdown must come back as the SAME poop, in the same place, with the
     * right age and the cleanliness it has already charged - otherwise
     * reboots either double-charge or reset the penalty.
     *
     * Compressed to fit the 384 B budget: age in minutes (45 days max),
     * drained in whole points (caps are 15/30), position halved (368x448
     * fits in 0..255 at /2, which is 2 px of placement accuracy). */
    uint8_t  bathroom;            /* 0..100                                 */
    uint16_t accidents;
    uint8_t  mess_type[4];        /* 0 none, 1 food, 2 poop                 */
    uint8_t  mess_food[4];        /* food_t in low bits, bit7 = bitten      */
    uint16_t mess_age_min[4];
    uint8_t  mess_drained[4];
    uint8_t  mess_x2[4], mess_y2[4];
} save_t;

#define SAVE_V1_SIZE 332          /* frozen: sizeof(save_t) at schema 1     */
#define MESS_BITTEN_BIT 0x80

typedef enum {
    LOAD_OK = 0,        /* clean read at the current schema      */
    LOAD_MIGRATED,      /* older schema, migrated forward        */
    LOAD_EMPTY,         /* no save present - first boot ever     */
    LOAD_CORRUPT,       /* bad CRC / size - defaults substituted */
    LOAD_FUTURE         /* newer schema - defaults substituted   */
} load_result_t;

void          storage_init(void);
load_result_t storage_load(save_t *out);
bool          storage_save(const save_t *in, bool force);
void          storage_defaults(save_t *out);
bool          storage_wipe(void);

/* Diagnostics */
uint32_t storage_crc32(const uint8_t *data, size_t len);

/* TEST ONLY: writes a synthetic, CRC-valid schema-1 blob so the v1 -> v2
 * migration can actually be exercised on hardware. A migration path that has
 * never been run is a guess, not a feature. */
bool storage_write_fake_v1(float hunger, float weight_g, uint16_t days);
uint32_t storage_write_count(void);      /* writes since boot */
uint32_t storage_skipped_count(void);    /* shadow-compare hits */
const char *storage_load_result_str(load_result_t r);

#ifdef __cplusplus
}
#endif
