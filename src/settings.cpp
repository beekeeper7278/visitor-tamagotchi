/* settings - device settings + tilt calibration in their own NVS key. */

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "storage.h"
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
} cfg_t;

static Preferences s_prefs;
static cfg_t       s_cfg;
static bool        s_open;

static const size_t ST_CRC_OFF = offsetof(cfg_t, crc32) + sizeof(uint32_t);

static uint32_t cfg_crc(void)
{
    return storage_crc32((const uint8_t *)&s_cfg + ST_CRC_OFF,
                         sizeof(s_cfg) - ST_CRC_OFF);
}

static void cfg_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.version    = SETTINGS_VERSION;
    s_cfg.size       = sizeof(cfg_t);
    s_cfg.volume     = 2;          /* VOL_MED - audible but not startling  */
    s_cfg.gravity_on = 1;          /* brief says default ON                */
    s_cfg.cal_valid  = 0;          /* flat-on-the-table until told otherwise */
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

    cfg_t in;
    const size_t got = s_prefs.getBytes(ST_KEY, &in, sizeof(in));
    if (got != sizeof(in)) {
        Serial.println("SETTINGS: no stored record - writing defaults");
        cfg_save();
        return;
    }
    const cfg_t save = s_cfg;
    s_cfg = in;
    const uint32_t want = cfg_crc();
    if (in.version != SETTINGS_VERSION || in.size != sizeof(cfg_t) ||
        in.crc32 != want) {
        Serial.printf("SETTINGS: record rejected (v%u size %u crc %s) - defaults\n",
                      in.version, in.size, in.crc32 == want ? "ok" : "BAD");
        s_cfg = save;
        cfg_save();
        return;
    }
    /* A stored value outside its range is treated as corruption of that FIELD
     * only, not of the record - losing a good calibration because the volume
     * byte is nonsense would be a worse outcome than clamping it. */
    if (s_cfg.volume > 3) s_cfg.volume = 2;
    if (s_cfg.gravity_on > 1) s_cfg.gravity_on = 1;
    Serial.printf("SETTINGS: loaded - volume %u, gravity %s, calibration %s\n",
                  s_cfg.volume, s_cfg.gravity_on ? "ON" : "OFF",
                  s_cfg.cal_valid ? "stored" : "none");
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
    Serial.println("  NOT part of save_t: device-scoped, survives a new Visitor");
    Serial.println("-----------------------------------------------------------");
}
