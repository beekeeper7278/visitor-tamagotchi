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

    /* --- migration chain: v(n) -> v(n+1) -----------------------------------
     * Nothing to do yet at schema 1. The chain is written now so that the
     * first time the struct changes, the shape to add to is already here
     * and obvious.
     *
     *   if (cand->schema == 1) { ...copy v1 fields into out...; cand->schema = 2; }
     *   if (cand->schema == 2) { ... }
     */
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
