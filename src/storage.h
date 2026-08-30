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

/* --- SAVE SIZE BUDGET ---------------------------------------------------
 * RAISED 384 -> 448 at schema 3, deliberately.
 *
 * Schema 3 measures 397 bytes. The old 384 figure was an early design target
 * from Phase 1, not a hardware limit, and Phase 8 needed explicitly-named
 * personality, evolution-path and discipline-history fields. The alternative
 * was semantic field reuse, which is exactly what this schema was rewritten
 * to remove - saving 13 bytes by storing a trait in a field called kid_form
 * is the wrong trade.
 *
 * NVS cost: the blob is stored in 32-byte entries, so 397 bytes occupies 13
 * entries = 416 bytes plus index overhead, call it ~450 bytes per save. The
 * nvs partition is 0x5000 = 20,480 bytes; NVS keeps one page for compaction,
 * leaving roughly 16 KB usable. The live save plus the game-records key
 * (~40 B) therefore use about 3% of it. Visit Records in Phase 9 have ample
 * room even at generous retention.
 *
 * The shadow copy used for write suppression lives in RAM, not NVS. */
#define SAVE_SIZE_BUDGET 448

#define SAVE_SCHEMA_VERSION 8

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

    /* --- schema 3 additions ------------------------------------------------
     * Phase 8 state gets EXPLICIT names. An earlier version rode personality
     * in kid_form/teen_form and mischief in pending_need because those bytes
     * were spare - but a trait stored in a field called kid_form is a trap
     * for every future reader of the migration, the Journal and any debug
     * session. A few extra bytes is the cheaper side of that trade.
     *
     * pending_need keeps its real meaning again. */
    uint8_t  personality_trait_1;
    uint8_t  personality_trait_2;
    uint8_t  mischief_tendency;      /* 0..100 hidden tendency             */
    uint16_t food_history[3];        /* burger / fruit / cake, lifetime    */
    uint16_t stage_start_day;        /* for per-stage rate denominators    */
    uint16_t stage_day_entered[5];   /* day each stage was entered         */
    float    acc_hours;              /* hours of accumulator sampling      */
    uint8_t  evo_announce;           /* a form change is waiting to be SHOWN */
    uint8_t  evo_path[4];            /* forms held at baby/kid/teen/adult  */
    uint16_t disc_opportunities;     /* lifetime, for the Journal          */
    uint16_t disc_ignored;           /* misbehaviour left uncorrected      */

    /* --- schema 4: the egg ------------------------------------------------
     * Persisted so a power cut during the five-minute hatch does not lose
     * the egg or restart its timer. */
    uint8_t  egg_color;
    uint8_t  egg_choice;
    uint32_t egg_hatch_ts;
    float    bath_target_h;   /* schema 5: randomised cycle target */

    /* --- schema 6: the variable visit -------------------------------------
     * depart_day is PERSISTED rather than recomputed at boot. If it were
     * recomputed the 36-hour lock would mean nothing across a power cycle:
     * a child could reboot their way to a different goodbye date. Once the
     * lock engages, this value is the answer and the save is what carries it.
     *
     * depart_due_ts is set the moment the date is reached, whether or not
     * anyone was there to see it - so a held farewell knows how long it has
     * been waiting, and the wait cap is measured from a real timestamp
     * rather than from millis(), which resets on every boot. */
    float    depart_day;      /* projected departure, in fractional age days */
    uint32_t depart_due_ts;   /* unix seconds when it fell due; 0 = not yet  */
    uint8_t  depart_locked;   /* 1 once inside VISIT_DEPART_LOCK_HOURS       */
    uint8_t  stay_band;       /* 0 short 1 middle 2 long, resolved on depart */

    /* --- schema 7: identity, learned behaviour, dreams --------------------
     * gender_choice is what the player PICKED (it may be GENDER_SURPRISE);
     * gender is the RESOLVED value. Both are stored because the Journal has
     * to know whether the "It's a boy!" reveal was ever a surprise, and
     * because the pre-hatch selector has to redraw the player's own choice
     * after a power cut rather than showing them a resolved answer they were
     * not meant to see yet. Resolution happens once, at START.
     *
     * learned_mischief is the rolling discipline-response EMA. It is
     * persisted rather than recomputed because the whole point is that it
     * spans stages: a Kid's discipline history has to still be legible when
     * that Visitor is a Teen, and re-deriving it from disc_ignored /
     * disc_opportunities would lose the DECAY - a Visitor with a bad first
     * day and a perfect week would read the same as one with the reverse. */
    uint8_t  gender;
    uint8_t  gender_choice;
    float    learned_mischief;
    uint8_t  dream_id[3];     /* recent dreams, indices into the table       */
    uint8_t  dream_n;

    /* --- schema 8: the sleep period ---------------------------------------
     * Persisted so that repeated reboots during ONE night cannot each
     * produce a dream. Recomputing this at boot is not an option: the
     * accumulated duration and the already-dreamt flag ARE the history, and
     * "is the clock inside a sleep window right now" cannot reconstruct
     * either of them. See pet.h for the four faults this replaced. */
    uint32_t sleep_accum_sec;
    uint8_t  sleep_flags;
    uint8_t  pending_dream;
} save_t;

#define SAVE_V3_SIZE 397
/* schema 5 measures 407 and adds one float, so schema 4 was 403. Getting
 * this wrong rejects every v4 save as corrupt.
 *
 * FROZEN SIZES, and the arithmetic that produced each one. Every schema has
 * APPENDED ONLY, so each size is a strict byte prefix of the next and every
 * migration is "copy the old blob, zero the tail":
 *     v1 332
 *     v2 363  = 332 + 31   (bathroom, accidents, the four mess arrays)
 *     v3 397  = 363 + 34   (personality, evo_path, per-stage counters)
 *     v4 403  = 397 +  6   (egg_color, egg_choice, egg_hatch_ts)
 *     v5 407  = 403 +  4   (bath_target_h)
 *     v6 417  = 407 + 10   (depart_day, depart_due_ts, depart_locked,
 *                           stay_band)
 *     v7 427  = 417 + 10   (gender, gender_choice, learned_mischief,
 *                           dream_id[3], dream_n)
 *     v8 433  = 427 +  6   (sleep_accum_sec, sleep_flags, pending_dream)
 * 433 of a 448 budget leaves 15 bytes of headroom - still enough for the
 * Phase 10 volume / tilt / gravity settings, which are the only additions
 * currently foreseen. Getting one of these wrong rejects every save of that
 * version as corrupt - it has happened once already - so recheck the
 * arithmetic if you add a schema. */
#define SAVE_V4_SIZE 403   /* schema 4 adds egg_choice; see the migration */          /* frozen: sizeof(save_t) at schema 3    */
#define SAVE_V5_SIZE 407   /* frozen: sizeof(save_t) at schema 5          */
#define SAVE_V6_SIZE 417   /* frozen: sizeof(save_t) at schema 6          */
#define SAVE_V7_SIZE 427   /* frozen: sizeof(save_t) at schema 7          */

/* Fail the BUILD rather than discover an over-budget blob on hardware. */
#ifdef __cplusplus
static_assert(sizeof(save_t) <= SAVE_SIZE_BUDGET, "save_t exceeds its NVS budget");
/* The migration chain is "copy the old blob, zero the tail", which is only
 * valid while every schema APPENDS. Proving the arithmetic at build time is
 * cheaper than discovering on hardware that every v6 save reads as corrupt -
 * which is exactly how the SAVE_V4_SIZE mistake was found. */
static_assert(sizeof(save_t) == SAVE_V7_SIZE + 6,
              "schema 8 must append exactly 6 bytes to schema 7");
static_assert(SAVE_V7_SIZE == SAVE_V6_SIZE + 10,
              "schema 7 appended exactly 10 bytes to schema 6");
#endif

#define SAVE_V2_SIZE 363          /* frozen: sizeof(save_t) at schema 2    */

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

/* TEST ONLY: a synthetic, CRC-valid SCHEMA-5 blob carrying the state the
 * v5 -> v6 migration has to preserve - accumulators, personality, evo_path
 * and the journal - plus a hatch timestamp already past the new day-9
 * departure floor. A migration that has never been RUN is a guess, and this
 * is the one that had never been run. */
bool storage_write_fake_v5(uint32_t hatch_ts, float hunger, float care_happy,
                           uint8_t form_id, uint8_t trait_a, uint8_t trait_b);

/* TEST ONLY: the same fixture one schema later, for the v6 -> v7 hop. It
 * carries a LOCKED departure projection as well as the accumulators, the
 * personality and the evolution path, because "the pacing state survives the
 * migration" is the specific claim this hop has to prove. */
bool storage_write_fake_v6(uint32_t hatch_ts, float hunger, float care_happy,
                           uint8_t form_id, uint8_t trait_a, uint8_t trait_b);

/* TEST ONLY: the v7 -> v8 hop, the one the sleep-period fields added. Carries
 * the whole schema-7 tail (resolved Surprise identity, learned behaviour, a
 * dream ring) so "Phase 9.5 state survives" is proven rather than assumed. */
bool storage_write_fake_v7(uint32_t hatch_ts, float hunger, float care_happy,
                           uint8_t form_id, uint8_t trait_a, uint8_t trait_b);
uint32_t storage_write_count(void);      /* writes since boot */
uint32_t storage_skipped_count(void);    /* shadow-compare hits */
const char *storage_load_result_str(load_result_t r);

#ifdef __cplusplus
}
#endif
