/* settings - device settings + tilt calibration in their own NVS key. */

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "storage.h"
#include "rtc.h"
#include "settings.h"

#define ST_NS  "visitors"
#define ST_KEY "cfg"

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t size;
    uint32_t crc32;
    uint8_t  volume;        /* VOL_* ; default VOL_MED                     */
    uint8_t  gravity_on;    /* default 1                                   */
    uint8_t  cal_valid;
    uint8_t  pad;
    float    cal_right;     /* neutral offset, display frame               */
    float    cal_down;
    /* --- v2 additions, APPENDED ------------------------------------------
     * Appended, like every save_t schema, so a v1 record is a strict byte
     * prefix of a v2 one and the migration is "copy the old bytes, default
     * the tail". The previous loader threw the whole record away on a
     * version bump, which would have cost a parent their volume and a child
     * their tilt calibration for the sake of one new byte - the header has
     * always promised an append is safe, and now the code keeps that
     * promise. */
    uint8_t  clock_confirmed;
    uint8_t  _pad2[3];
    uint32_t clock_set_ts;  /* what the clock READ at the moment of confirmation */
} cfg_t;

/* FROZEN. sizeof(cfg_t) at v1: version+size+crc 8, volume/gravity/cal_valid/
 * pad 4, two floats 8. Getting this wrong rejects every v1 record. */
#define SETTINGS_V1_SIZE 20

/* Fail the BUILD, not the device. Getting either of these wrong rejects
 * every stored record of that version as corrupt - the same trap the save_t
 * SAVE_V*_SIZE constants document. */
static_assert(sizeof(cfg_t) == SETTINGS_V1_SIZE + 8,
              "settings v2 must APPEND exactly 8 bytes to v1");
static_assert(offsetof(cfg_t, clock_confirmed) == SETTINGS_V1_SIZE,
              "the v2 tail must start exactly where v1 ended");

static Preferences s_prefs;
static cfg_t       s_cfg;
static bool        s_open;

static const size_t ST_CRC_OFF = offsetof(cfg_t, crc32) + sizeof(uint32_t);

/* CRC over everything AFTER the crc32 field, for a record of `len` bytes.
 * The length is a parameter so a v1 blob can be checked against v1's own
 * extent while sitting in a v2-sized struct. */
static uint32_t cfg_crc_len(size_t len)
{
    return storage_crc32((const uint8_t *)&s_cfg + ST_CRC_OFF, len - ST_CRC_OFF);
}

static uint32_t cfg_crc(void) { return cfg_crc_len(sizeof(s_cfg)); }

static void cfg_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.version    = SETTINGS_VERSION;
    s_cfg.size       = sizeof(cfg_t);
    s_cfg.volume     = 2;          /* VOL_MED - audible but not startling  */
    s_cfg.gravity_on = 1;          /* brief says default ON                */
    s_cfg.cal_valid  = 0;          /* flat-on-the-table until told otherwise */
    /* DEFAULT 0, INCLUDING ON A MIGRATION FROM v1.
     *
     * The alternative - inferring "well, the RTC looks trusted and there is
     * a hatched Visitor, so somebody must have set it" - is fabricating a
     * confirmation nobody gave, which is precisely the failure mode this
     * flag exists to close. A device upgrading to this build asks for the
     * date once, at its next egg. A live Visitor is untouched: the gate is
     * pre-hatch only. */
    s_cfg.clock_confirmed = 0;
    s_cfg.clock_set_ts    = 0;
}

static void cfg_save(void)
{
    if (!s_open) return;
    s_cfg.version = SETTINGS_VERSION;
    s_cfg.size    = sizeof(cfg_t);
    s_cfg.crc32   = cfg_crc();
    const size_t n = s_prefs.putBytes(ST_KEY, &s_cfg, sizeof(s_cfg));
    if (n != sizeof(s_cfg)) Serial.println("SETTINGS: save FAILED");
}

void settings_begin(void)
{
    cfg_defaults();
    s_open = s_prefs.begin(ST_NS, false);
    if (!s_open) { Serial.println("SETTINGS: NVS open failed - defaults, not persisted"); return; }

    const size_t stored = s_prefs.getBytesLength(ST_KEY);
    if (stored != sizeof(cfg_t) && stored != SETTINGS_V1_SIZE) {
        Serial.printf("SETTINGS: no usable stored record (%u bytes) - writing defaults\n",
                      (unsigned)stored);
        cfg_save();
        return;
    }

    const cfg_t defaults_copy = s_cfg;
    /* Read into the v2-sized struct. A v1 blob fills the prefix and leaves
     * the appended tail at its default, which is exactly the migration. */
    if (s_prefs.getBytes(ST_KEY, &s_cfg, stored) != stored) {
        Serial.println("SETTINGS: read FAILED - defaults");
        s_cfg = defaults_copy;
        cfg_save();
        return;
    }

    const uint16_t got_ver  = s_cfg.version;
    const uint16_t got_size = s_cfg.size;
    const uint32_t got_crc  = s_cfg.crc32;
    const uint32_t want     = cfg_crc_len(stored);
    const bool plausible = (got_size == stored) &&
                           ((got_ver == 1 && stored == SETTINGS_V1_SIZE) ||
                            (got_ver == SETTINGS_VERSION && stored == sizeof(cfg_t)));
    if (!plausible || got_crc != want) {
        Serial.printf("SETTINGS: record rejected (v%u size %u crc %s) - defaults\n",
                      got_ver, got_size, got_crc == want ? "ok" : "BAD");
        s_cfg = defaults_copy;
        cfg_save();
        return;
    }
    if (got_ver != SETTINGS_VERSION) {
        s_cfg.clock_confirmed = defaults_copy.clock_confirmed;
        s_cfg.clock_set_ts    = defaults_copy.clock_set_ts;
        Serial.printf("SETTINGS: migrated v%u -> v%u, volume and calibration kept\n",
                      got_ver, SETTINGS_VERSION);
        cfg_save();
    }
    /* A stored value outside its range is treated as corruption of that FIELD
     * only, not of the record - losing a good calibration because the volume
     * byte is nonsense would be a worse outcome than clamping it. */
    if (s_cfg.volume > 3) s_cfg.volume = 2;
    if (s_cfg.gravity_on > 1) s_cfg.gravity_on = 1;
    if (s_cfg.clock_confirmed > 1) s_cfg.clock_confirmed = 0;
    Serial.printf("SETTINGS: loaded - volume %u, gravity %s, calibration %s, "
                  "clock %s\n",
                  s_cfg.volume, s_cfg.gravity_on ? "ON" : "OFF",
                  s_cfg.cal_valid ? "stored" : "none",
                  s_cfg.clock_confirmed ? "CONFIRMED" : "not confirmed");
}

uint8_t settings_volume(void) { return s_cfg.volume; }

void settings_set_volume(uint8_t v)
{
    if (v > 3) v = 2;
    if (v == s_cfg.volume) return;
    s_cfg.volume = v;
    cfg_save();
}

bool settings_gravity_on(void) { return s_cfg.gravity_on != 0; }

void settings_set_gravity(bool on)
{
    if ((uint8_t)on == s_cfg.gravity_on) return;
    s_cfg.gravity_on = on ? 1 : 0;
    cfg_save();
    Serial.printf("SETTINGS: gravity reactions %s\n", on ? "ON" : "OFF");
}

bool settings_cal_valid(void) { return s_cfg.cal_valid != 0; }

void settings_cal(float *right, float *down)
{
    if (right) *right = s_cfg.cal_valid ? s_cfg.cal_right : 0.0f;
    if (down)  *down  = s_cfg.cal_valid ? s_cfg.cal_down  : 0.0f;
}

void settings_set_cal(float right, float down)
{
    s_cfg.cal_right = right;
    s_cfg.cal_down  = down;
    s_cfg.cal_valid = 1;
    cfg_save();
    Serial.printf("SETTINGS: tilt calibrated - neutral right %+.3f down %+.3f\n",
                  (double)right, (double)down);
}

void settings_clear_cal(void)
{
    s_cfg.cal_valid = 0;
    s_cfg.cal_right = s_cfg.cal_down = 0.0f;
    cfg_save();
}

bool     settings_clock_confirmed(void) { return s_cfg.clock_confirmed != 0; }
uint32_t settings_clock_set_ts(void)    { return s_cfg.clock_set_ts; }

void settings_set_clock_confirmed(uint32_t confirmed_now_ts)
{
    const uint8_t on = confirmed_now_ts ? 1 : 0;
    if (on == s_cfg.clock_confirmed && s_cfg.clock_set_ts == confirmed_now_ts) return;
    s_cfg.clock_confirmed = on;
    s_cfg.clock_set_ts    = confirmed_now_ts;
    cfg_save();
    Serial.printf("SETTINGS: clock %s\n",
                  on ? "CONFIRMED by a human and verified" : "confirmation CLEARED");
}

void settings_report(void)
{
    Serial.println();
    Serial.println("=== SETTINGS ===============================================");
    Serial.printf("  record       : v%u, %u bytes, own NVS key %s/%s\n",
                  s_cfg.version, (unsigned)sizeof(cfg_t), ST_NS, ST_KEY);
    Serial.printf("  volume       : %u\n", s_cfg.volume);
    Serial.printf("  gravity      : %s\n", s_cfg.gravity_on ? "ON" : "OFF");
    if (s_cfg.cal_valid)
        Serial.printf("  calibration  : right %+.3f  down %+.3f\n",
                      (double)s_cfg.cal_right, (double)s_cfg.cal_down);
    else
        Serial.println("  calibration  : none (assuming flat)");
    if (s_cfg.clock_confirmed) {
        char b[40];
        rtc_format_friendly(s_cfg.clock_set_ts, b, sizeof(b));
        Serial.printf("  clock        : CONFIRMED (read %s when set)\n", b);
    } else {
        Serial.println("  clock        : NOT confirmed - a new Visitor cannot START");
    }
    Serial.println("  NOT part of save_t: device-scoped, survives a new Visitor");
    Serial.println("-----------------------------------------------------------");
}
