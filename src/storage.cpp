#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "storage.h"

static Preferences s_prefs;
static bool     s_open        = false;
static save_t   s_shadow;                 /* last blob actually written */
static bool     s_shadow_valid = false;
static uint32_t s_writes       = 0;
static uint32_t s_skipped      = 0;
static uint32_t s_last_write_ms = 0;

/* ------------------------------------------------------------------------
 * CRC32 (IEEE 802.3, reflected) - table-free, ~1us for a 340-byte blob.
 * ---------------------------------------------------------------------- */
uint32_t storage_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

/* CRC covers everything after the crc32 field itself. */
static const size_t CRC_OFFSET = offsetof(save_t, crc32) + sizeof(uint32_t);

static uint32_t compute_crc(const save_t *s)
{
    return storage_crc32((const uint8_t *)s + CRC_OFFSET,
                         sizeof(save_t) - CRC_OFFSET);
}

/* ------------------------------------------------------------------------
 * Defaults - a pre-hatch visitor with no clock set yet.
 * ---------------------------------------------------------------------- */
void storage_defaults(save_t *o)
{
    memset(o, 0, sizeof(*o));
    o->schema      = SAVE_SCHEMA_VERSION;
    o->struct_size = sizeof(save_t);

    o->hunger = o->happiness = o->cleanliness = o->energy = 100.0f;
    o->discipline = 50.0f;
    o->weight_g   = 50.0f;

    /* Accumulators start neutral, not perfect: a brand new pet has no care
     * history, and seeding them at 100 would hand out a free top form. */
    o->care_happy = o->care_fed = o->care_clean = 50.0f;
    o->care_sleep = o->care_discipline = 50.0f;
    o->nutrition  = 0.0f;

    o->flags = SF_LIGHTS_ON;     /* lights start on; bedtime is a choice */
}

/* ------------------------------------------------------------------------
 * Quantise floats to 0.1 before serialising.
 *
 * Without this the shadow-compare never fires: raw float decay means the
 * blob differs every single tick and we would write to flash constantly.
 * ---------------------------------------------------------------------- */
static float q(float v) { return roundf(v * 10.0f) / 10.0f; }

static void quantise(save_t *s)
{
    s->hunger      = q(s->hunger);
    s->happiness   = q(s->happiness);
    s->discipline  = q(s->discipline);
    s->cleanliness = q(s->cleanliness);
    s->energy      = q(s->energy);
    s->weight_g    = q(s->weight_g);
    s->care_happy      = q(s->care_happy);
    s->care_fed        = q(s->care_fed);
    s->care_clean      = q(s->care_clean);
    s->care_sleep      = q(s->care_sleep);
    s->care_discipline = q(s->care_discipline);
    s->nutrition       = q(s->nutrition);
}

/* ------------------------------------------------------------------------ */

void storage_init(void)
{
    s_open = s_prefs.begin(NVS_NAMESPACE, false);
    s_shadow_valid = false;
}

load_result_t storage_load(save_t *out)
{
    storage_defaults(out);
    if (!s_open) return LOAD_CORRUPT;

    const size_t stored = s_prefs.getBytesLength(NVS_KEY_SAVE);
    if (stored == 0) return LOAD_EMPTY;

    /* Read into a scratch buffer sized by what is actually stored, so an
     * older, SMALLER struct can still be inspected and migrated. */
    if (stored > sizeof(save_t) * 2) return LOAD_CORRUPT;   /* nonsense size */

    uint8_t scratch[sizeof(save_t) * 2];
    memset(scratch, 0, sizeof(scratch));
    if (s_prefs.getBytes(NVS_KEY_SAVE, scratch, stored) != stored)
        return LOAD_CORRUPT;

    save_t *cand = (save_t *)scratch;

    if (cand->schema > SAVE_SCHEMA_VERSION) {
        /* Downgrade. We cannot know what the newer fields meant, so we do
         * not pretend to. Defaults already in *out. */
        return LOAD_FUTURE;
    }

    if (cand->schema == SAVE_SCHEMA_VERSION) {
        if (cand->struct_size != sizeof(save_t) || stored != sizeof(save_t))
            return LOAD_CORRUPT;
        const uint32_t want = compute_crc(cand);
        if (want != cand->crc32) return LOAD_CORRUPT;
        memcpy(out, cand, sizeof(save_t));
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_OK;
    }

    /* --- migration chain: v(n) -> v(n+1) ---------------------------------- */

    /* The chain runs v1 -> v2 -> ... -> v6, each step a prefix copy plus a
     * zeroed tail, because every schema has appended only. A v1 save
     * therefore reaches the current schema through one copy rather than
     * needing its own path per hop.
     *
     * NOTHING HERE REPLAYS HISTORY. The migration never sets evo_announce,
     * never re-runs evolve_pick_form() for a stage already in evo_path[], and
     * never re-adds a journal entry: the forms the Visitor already holds are
     * its actual past, and a migration that re-announced them would show a
     * child a transformation that happened days ago. Stage is recomputed from
     * the clock at boot by pet_apply_stage_for_day(), which walks the
     * boundaries in order and records each against the day it belongs to. */
    if (cand->schema == 7) {
        if (cand->struct_size != SAVE_V7_SIZE || stored != SAVE_V7_SIZE)
            return LOAD_CORRUPT;
        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V7_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;
        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V7_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;
        out->struct_size = sizeof(save_t);
        /* A ZEROED TAIL IS EXACTLY RIGHT HERE, for once: no sleep period is
         * open, none has accumulated, and nothing is waiting to be said. The
         * next bedtime opens the first tracked period. Deliberately noted
         * rather than left silent - every other schema-7 field needed
         * seeding, and "this one genuinely does not" is worth stating. */
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    if (cand->schema == 6) {
        if (cand->struct_size != SAVE_V6_SIZE || stored != SAVE_V6_SIZE)
            return LOAD_CORRUPT;
        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V6_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;
        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V6_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;
        out->struct_size = sizeof(save_t);
        /* A zeroed tail reads as gender BOY / choice BOY, learned_mischief
         * 0.0 and no dreams. Two of those three are wrong for a Visitor that
         * predates them, so they are set explicitly rather than left at the
         * memset value:
         *   - gender_choice becomes SURPRISE, because the player never made
         *     a choice, and gender is rolled ONCE here and then persisted
         *     like any other resolved surprise. Leaving it at BOY would tell
         *     every existing Visitor's owner something the game never asked
         *     them and never showed them;
         *   - learned_mischief starts NEUTRAL. Zero would mean "every
         *     discipline window this Visitor ever had was corrected", which
         *     is an unearned reward for a history we do not have.
         * A zero dream ring is genuinely correct: no dreams have happened. */
        out->gender_choice   = GENDER_SURPRISE;
        out->gender          = (uint8_t)(esp_random() & 1u);
        out->learned_mischief = LEARN_START;
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    if (cand->schema == 5) {
        if (cand->struct_size != SAVE_V5_SIZE || stored != SAVE_V5_SIZE)
            return LOAD_CORRUPT;
        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V5_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;
        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V5_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;
        out->struct_size = sizeof(save_t);
        out->gender_choice   = GENDER_SURPRISE;
        out->gender          = (uint8_t)(esp_random() & 1u);
        out->learned_mischief = LEARN_START;
        /* depart_day 0 means "never projected". The first re-evaluation
         * seeds it from the care history the save already carries, and the
         * minimum-notice floor stops a save that is ALREADY past day 9 from
         * being handed a departure date in the past. A v5 Visitor was living
         * under the fixed 21-day visit, so it has no lock to preserve and
         * depart_locked / depart_due_ts are correctly zero. */
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    if (cand->schema == 4) {
        if (cand->struct_size != SAVE_V4_SIZE || stored != SAVE_V4_SIZE)
            return LOAD_CORRUPT;
        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V4_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;
        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V4_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;
        out->struct_size = sizeof(save_t);
        out->gender_choice   = GENDER_SURPRISE;
        out->gender          = (uint8_t)(esp_random() & 1u);
        out->learned_mischief = LEARN_START;
        /* bath_target_h zero means "draw one on the next cycle" - correct
         * for a save that predates randomised targets. */
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    if (cand->schema == 3) {
        if (cand->struct_size != SAVE_V3_SIZE || stored != SAVE_V3_SIZE)
            return LOAD_CORRUPT;
        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V3_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;
        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V3_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;
        out->struct_size = sizeof(save_t);
        out->gender_choice   = GENDER_SURPRISE;
        out->gender          = (uint8_t)(esp_random() & 1u);
        out->learned_mischief = LEARN_START;
        /* zeroed egg fields mean "no egg pending", which is right for a save
         * whose Visitor had already hatched */
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    if (cand->schema == 2) {
        if (cand->struct_size != SAVE_V2_SIZE || stored != SAVE_V2_SIZE)
            return LOAD_CORRUPT;
        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V2_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;

        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V2_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;
        out->struct_size = sizeof(save_t);
        out->gender_choice   = GENDER_SURPRISE;
        out->gender          = (uint8_t)(esp_random() & 1u);
        out->learned_mischief = LEARN_START;
        /* New fields default to zero, which is the correct v2 meaning: no
         * personality recorded yet, no evolution path, nothing to announce.
         * pet_init() will roll a personality if these are still blank. */
        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    if (cand->schema == 1) {
        /* v1 is a strict byte prefix of v2 (schema 2 fields were appended),
         * so the migration is a prefix copy plus a zeroed tail. Validate the
         * v1 CRC over the v1 LENGTH first - migrating unverified bytes would
         * turn a corrupt save into a plausible-looking one. */
        if (cand->struct_size != SAVE_V1_SIZE || stored != SAVE_V1_SIZE)
            return LOAD_CORRUPT;

        const uint32_t want = storage_crc32(scratch + CRC_OFFSET,
                                            SAVE_V1_SIZE - CRC_OFFSET);
        if (want != cand->crc32) return LOAD_CORRUPT;

        memset(out, 0, sizeof(save_t));
        memcpy(out, cand, SAVE_V1_SIZE);
        out->schema      = SAVE_SCHEMA_VERSION;   /* v1 -> ... -> v7 in one hop: every step
                                    * are "append and zero", so the result is
                                    * identical to running them separately */
        out->struct_size = sizeof(save_t);
        /* New v2 fields are already zero, which is the correct v1 meaning:
         * no bathroom need recorded, and no individually-tracked messes. */
        out->bathroom = 0;
        out->accidents = 0;
        memset(out->mess_type, 0, sizeof(out->mess_type));
        out->gender_choice    = GENDER_SURPRISE;
        out->gender           = (uint8_t)(esp_random() & 1u);
        out->learned_mischief = LEARN_START;

        memcpy(&s_shadow, out, sizeof(save_t));
        s_shadow_valid = true;
        return LOAD_MIGRATED;
    }

    return LOAD_CORRUPT;
}

bool storage_save(const save_t *in, bool force)
{
    if (!s_open) return false;

    /* Hard floor between writes. Protects flash from any caller that gets
     * over-eager; `force` is only for shutdown paths. */
    const uint32_t now = millis();
    if (!force && s_last_write_ms != 0 &&
        (now - s_last_write_ms) < SAVE_MIN_INTERVAL_MS) {
        return false;
    }

    save_t blob;
    memcpy(&blob, in, sizeof(blob));
    blob.schema      = SAVE_SCHEMA_VERSION;
    blob.struct_size = sizeof(save_t);
    quantise(&blob);
    blob.crc32 = compute_crc(&blob);

    /* Shadow compare: identical blob means the flash write buys nothing. */
    if (s_shadow_valid && memcmp(&blob, &s_shadow, sizeof(blob)) == 0) {
        s_skipped++;
        return true;
    }

    const size_t w = s_prefs.putBytes(NVS_KEY_SAVE, &blob, sizeof(blob));
    if (w != sizeof(blob)) return false;

    memcpy(&s_shadow, &blob, sizeof(blob));
    s_shadow_valid  = true;
    s_last_write_ms = now;
    s_writes++;
    return true;
}

bool storage_wipe(void)
{
    if (!s_open) return false;
    s_shadow_valid = false;
    return s_prefs.clear();
}

bool storage_write_fake_v5(uint32_t hatch_ts, float hunger, float care_happy,
                           uint8_t form_id, uint8_t trait_a, uint8_t trait_b)
{
    if (!s_open) return false;

    /* v5 is a strict byte PREFIX of v6, so a v6-shaped struct truncated to
     * SAVE_V5_SIZE is a byte-exact v5 blob. Writing it that way rather than
     * hand-packing means the fixture cannot drift away from the real layout. */
    uint8_t buf[SAVE_V5_SIZE];
    memset(buf, 0, sizeof(buf));
    save_t *v = (save_t *)buf;

    v->schema      = 5;
    v->struct_size = SAVE_V5_SIZE;
    v->hatch_ts    = hatch_ts;
    v->last_sim_ts = hatch_ts;
    v->hunger      = hunger;
    v->happiness   = 70.0f;
    v->cleanliness = 65.0f;
    v->energy      = 60.0f;
    v->discipline  = 55.0f;
    v->weight_g    = 61.0f;
    v->stage       = 4;            /* Adult, already past the new boundaries */
    v->form_id     = form_id;
    v->days_alive_max = 11;
    v->care_happy  = care_happy;
    v->care_fed    = 72.0f;
    v->care_clean  = 68.0f;
    v->care_sleep  = 70.0f;
    v->care_discipline = 60.0f;
    v->personality_trait_1 = trait_a;
    v->personality_trait_2 = trait_b;
    v->evo_path[0] = 1; v->evo_path[1] = 2;
    v->evo_path[2] = 3; v->evo_path[3] = form_id;
    v->acc_hours   = 240.0f;
    v->evo_announce = 0;           /* nothing outstanding - must STAY 0 */
    v->journal[0].ts = hatch_ts;
    v->journal[0].type = 0;        /* JM_HATCHED */
    v->bath_target_h = 4.25f;

    v->crc32 = storage_crc32(buf + CRC_OFFSET, SAVE_V5_SIZE - CRC_OFFSET);

    const size_t w = s_prefs.putBytes(NVS_KEY_SAVE, buf, SAVE_V5_SIZE);
    s_shadow_valid = false;
    if (w != SAVE_V5_SIZE)
        Serial.printf("fake v5 write: %u of %u bytes\n", (unsigned)w,
                      (unsigned)SAVE_V5_SIZE);
    return w == SAVE_V5_SIZE;
}

bool storage_write_fake_v7(uint32_t hatch_ts, float hunger, float care_happy,
                           uint8_t form_id, uint8_t trait_a, uint8_t trait_b)
{
    if (!s_open) return false;

    /* v7 is a strict byte PREFIX of v8, so a v8-shaped struct truncated to
     * SAVE_V7_SIZE IS a v7 blob. It carries the schema-7 tail - a resolved
     * Surprise identity, a learned-behaviour value and a dream ring - because
     * "the Phase 9.5 state survives the hop" is what this one has to prove. */
    uint8_t buf[SAVE_V7_SIZE];
    memset(buf, 0, sizeof(buf));
    save_t *v = (save_t *)buf;

    v->schema      = 7;
    v->struct_size = SAVE_V7_SIZE;
    v->hatch_ts    = hatch_ts;
    v->last_sim_ts = hatch_ts;
    v->hunger      = hunger;
    v->happiness   = 70.0f;
    v->cleanliness = 65.0f;
    v->energy      = 60.0f;
    v->discipline  = 55.0f;
    v->weight_g    = 61.0f;
    v->stage       = 4;            /* Adult */
    v->form_id     = form_id;
    v->days_alive_max = 11;
    v->care_happy  = care_happy;
    v->care_fed    = 72.0f;
    v->care_clean  = 68.0f;
    v->care_sleep  = 70.0f;
    v->care_discipline = 60.0f;
    v->personality_trait_1 = trait_a;
    v->personality_trait_2 = trait_b;
    v->evo_path[0] = 0; v->evo_path[1] = 1;
    v->evo_path[2] = 3; v->evo_path[3] = form_id;
    v->acc_hours   = 240.0f;
    v->evo_announce = 0;
    v->journal[0].ts = hatch_ts;
    v->journal[0].type = 0;        /* JM_HATCHED */
    v->bath_target_h = 4.25f;
    v->depart_day    = 12.5f;
    v->depart_locked = 1;
    v->egg_color     = 2;
    v->egg_choice    = EGG_PALETTE_COUNT;   /* Surprise, already resolved */
    /* --- the schema-7 tail, which must come through untouched --- */
    v->gender          = GENDER_GIRL;
    v->gender_choice   = GENDER_SURPRISE;
    v->learned_mischief = 73.5f;
    v->dream_id[0] = 4; v->dream_id[1] = 11; v->dream_id[2] = 2;
    v->dream_n     = 3;

    v->crc32 = storage_crc32(buf + CRC_OFFSET, SAVE_V7_SIZE - CRC_OFFSET);

    const size_t w = s_prefs.putBytes(NVS_KEY_SAVE, buf, SAVE_V7_SIZE);
    s_shadow_valid = false;
    if (w != SAVE_V7_SIZE)
        Serial.printf("fake v7 write: %u of %u bytes\n", (unsigned)w,
                      (unsigned)SAVE_V7_SIZE);
    return w == SAVE_V7_SIZE;
}

bool storage_write_fake_v6(uint32_t hatch_ts, float hunger, float care_happy,
                           uint8_t form_id, uint8_t trait_a, uint8_t trait_b)
{
    if (!s_open) return false;

    /* Same trick as the v5 fixture: v6 is a strict byte PREFIX of v7, so a
     * v7-shaped struct truncated to SAVE_V6_SIZE IS a v6 blob. Hand-packing
     * would let the fixture drift away from the real layout, and a fixture
     * that is subtly not what it claims proves nothing. */
    uint8_t buf[SAVE_V6_SIZE];
    memset(buf, 0, sizeof(buf));
    save_t *v = (save_t *)buf;

    v->schema      = 6;
    v->struct_size = SAVE_V6_SIZE;
    v->hatch_ts    = hatch_ts;
    v->last_sim_ts = hatch_ts;
    v->hunger      = hunger;
    v->happiness   = 70.0f;
    v->cleanliness = 65.0f;
    v->energy      = 60.0f;
    v->discipline  = 55.0f;
    v->weight_g    = 61.0f;
    v->stage       = 4;            /* Adult */
    v->form_id     = form_id;
    v->days_alive_max = 11;
    v->care_happy  = care_happy;
    v->care_fed    = 72.0f;
    v->care_clean  = 68.0f;
    v->care_sleep  = 70.0f;
    v->care_discipline = 60.0f;
    v->personality_trait_1 = trait_a;
    v->personality_trait_2 = trait_b;
    v->evo_path[0] = 0; v->evo_path[1] = 1;      /* Baby, Good Kid,        */
    v->evo_path[2] = 3; v->evo_path[3] = form_id;/* Bright Teen, this      */
    v->acc_hours   = 240.0f;
    v->evo_announce = 0;           /* nothing outstanding - must STAY 0 */
    v->journal[0].ts = hatch_ts;
    v->journal[0].type = 0;        /* JM_HATCHED */
    v->bath_target_h = 4.25f;
    /* A LOCKED, already-projected departure: the whole point of testing this
     * hop is proving the pacing state survives it intact. */
    v->depart_day    = 12.5f;
    v->depart_locked = 1;
    v->egg_color     = 2;
    v->egg_choice    = 2;

    v->crc32 = storage_crc32(buf + CRC_OFFSET, SAVE_V6_SIZE - CRC_OFFSET);

    const size_t w = s_prefs.putBytes(NVS_KEY_SAVE, buf, SAVE_V6_SIZE);
    s_shadow_valid = false;
    if (w != SAVE_V6_SIZE)
        Serial.printf("fake v6 write: %u of %u bytes\n", (unsigned)w,
                      (unsigned)SAVE_V6_SIZE);
    return w == SAVE_V6_SIZE;
}

bool storage_write_fake_v1(float hunger, float weight_g, uint16_t days)
{
    if (!s_open) return false;

    uint8_t buf[SAVE_V1_SIZE];
    memset(buf, 0, sizeof(buf));
    save_t *v = (save_t *)buf;          /* v1 is a prefix of v2 */

    v->schema      = 1;
    v->struct_size = SAVE_V1_SIZE;
    v->hunger      = hunger;
    v->happiness   = 64.0f;
    v->discipline  = 40.0f;
    v->cleanliness = 55.0f;
    v->energy      = 70.0f;
    v->weight_g    = weight_g;
    v->days_alive_max = days;
    v->stage       = 1;
    v->meals       = 7;
    v->flags       = SF_HATCHED;

    v->crc32 = storage_crc32(buf + CRC_OFFSET, SAVE_V1_SIZE - CRC_OFFSET);

    const size_t w = s_prefs.putBytes(NVS_KEY_SAVE, buf, SAVE_V1_SIZE);
    s_shadow_valid = false;             /* shadow no longer matches NVS */
    Serial.printf("TEST: wrote a synthetic schema-1 save (%u bytes)\n",
                  (unsigned)SAVE_V1_SIZE);
    return w == SAVE_V1_SIZE;
}

uint32_t storage_write_count(void)   { return s_writes;  }
uint32_t storage_skipped_count(void) { return s_skipped; }

const char *storage_load_result_str(load_result_t r)
{
    switch (r) {
        case LOAD_OK:       return "OK";
        case LOAD_MIGRATED: return "MIGRATED";
        case LOAD_EMPTY:    return "EMPTY (first boot)";
        case LOAD_CORRUPT:  return "CORRUPT -> safe reset";
        case LOAD_FUTURE:   return "FUTURE SCHEMA -> safe reset";
    }
    return "?";
}
