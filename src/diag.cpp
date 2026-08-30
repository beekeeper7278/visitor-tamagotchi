#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "board_pins.h"
#include "config.h"
#include "bsp.h"
#include "storage.h"
#include "forms.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "ui_diag.h"
#include "scr_main.h"
#include "strings.h"
#include "rtc.h"
#include "sim.h"
#include "persist.h"
#include "setclock.h"
#include "games.h"
#include "gamerec.h"
#include "evolve.h"
#include "discipline.h"
#include "journal.h"
#include "visitrec.h"
#include "farewell.h"
#include "dialogue.h"
#include "menu.h"
#include "pages.h"
#include "pet.h"
#include "care.h"
#include "diag.h"

#define LINE "-----------------------------------------------------------"

/* ==========================================================================
 * Banner
 * ======================================================================= */
void diag_banner(void)
{
    Serial.println();
    Serial.println("===========================================================");
    Serial.println("  VISITOR - Waveshare ESP32-S3-Touch-AMOLED-1.8 V2");
    Serial.printf ("  %s   (CO5300 + CST820)\n", VISITOR_VERSION);
    Serial.println("===========================================================");
    Serial.printf("Chip      : %s rev%d, %d core(s) @ %lu MHz\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  (unsigned long)getCpuFrequencyMhz());
    Serial.printf("Flash     : %lu bytes\n", (unsigned long)ESP.getFlashChipSize());
    Serial.printf("PSRAM     : %lu bytes total, %lu free\n",
                  (unsigned long)ESP.getPsramSize(), (unsigned long)ESP.getFreePsram());
    Serial.printf("Int. heap : %lu free / %lu largest block\n",
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    Serial.println(LINE);
}

/* ==========================================================================
 * MEASUREMENT 1 - I2C scan
 * ======================================================================= */
static const char *i2c_name(uint8_t a)
{
    switch (a) {
        case I2C_ADDR_TOUCH:   return "CST820 touch";
        case I2C_ADDR_CODEC:   return "ES8311 audio codec";
        case I2C_ADDR_IOEXP:   return "TCA9554 IO expander";
        case I2C_ADDR_PMIC:    return "AXP2101 PMIC";
        case I2C_ADDR_RTC:     return "PCF85063 RTC";
        case I2C_ADDR_IMU:     return "QMI8658 IMU";
        case I2C_ADDR_UNKNOWN: return "UNIDENTIFIED - do not write";
        default:               return "*** UNEXPECTED ***";
    }
}

void diag_i2c_report(void)
{
    const bsp_status_t *st = bsp_status();
    Serial.println("[1] I2C SCAN");
    Serial.printf("    bus: SDA=%d SCL=%d @ %d Hz -> %s\n",
                  BSP_I2C_SDA, BSP_I2C_SCL, BSP_I2C_HZ,
                  st->i2c_ok ? "up" : "FAILED");
    if (!st->i2c_ok) { Serial.println(LINE); return; }

    Serial.printf("    devices found: %u\n", st->i2c_count);
    for (uint8_t i = 0; i < st->i2c_count; i++)
        Serial.printf("      0x%02X  %s\n", st->i2c_found[i], i2c_name(st->i2c_found[i]));

    /* Cross-check against the seven documented addresses */
    const uint8_t expect[] = { I2C_ADDR_TOUCH, I2C_ADDR_CODEC, I2C_ADDR_IOEXP,
                               I2C_ADDR_PMIC,  I2C_ADDR_RTC,   I2C_ADDR_IMU,
                               I2C_ADDR_UNKNOWN };
    for (uint8_t e = 0; e < sizeof(expect); e++) {
        bool seen = false;
        for (uint8_t i = 0; i < st->i2c_count; i++)
            if (st->i2c_found[i] == expect[e]) { seen = true; break; }
        if (!seen) Serial.printf("    !! EXPECTED 0x%02X (%s) NOT FOUND\n",
                                 expect[e], i2c_name(expect[e]));
    }
    Serial.printf("    TCA9554 reset sequence (bits 0,1,2,6 low/20ms/high/50ms): %s\n",
                  st->ioexp_ok ? "applied" : "NOT APPLIED");
    Serial.println(LINE);
}

/* ==========================================================================
 * MEASUREMENT 2 - flush throughput through the real LVGL path
 * ======================================================================= */
void diag_flush_report(void)
{
    Serial.println("[2] DISPLAY THROUGHPUT");

    if (!bsp_display_available()) {
        Serial.println("    BLOCKED: display not brought up.");
        Serial.println("    Reason : QSPI bus pins are unresolved.");
        Serial.println("             Set BSP_QSPI_* in include/board_pins.h and");
        Serial.println("             flip BSP_QSPI_VERIFIED to 1, then re-run.");
        Serial.println(LINE);
        return;
    }

    bsp_flush_stats_reset();
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < DIAG_FPS_SAMPLE_FRAMES; i++) {
        lv_obj_invalidate(lv_scr_act());       /* force a full-screen redraw */
        lv_refr_now(NULL);
    }
    const int64_t wall_us = esp_timer_get_time() - t0;

    bsp_flush_stats_t s; bsp_flush_stats_get(&s);
    if (s.total_us == 0 || s.flushes == 0) {
        Serial.println("    no flushes recorded");
        Serial.println(LINE);
        return;
    }

    const double bytes   = (double)s.total_px * sizeof(lv_color_t);
    const double mbps    = bytes / (double)s.total_us;               /* B/us == MB/s */
    const double frame_b = (double)BSP_LCD_W * BSP_LCD_H * sizeof(lv_color_t);
    const double fps_bus = mbps * 1e6 / frame_b;
    const double fps_real= (double)DIAG_FPS_SAMPLE_FRAMES * 1e6 / (double)wall_us;

    Serial.printf("    frames rendered   : %d\n", DIAG_FPS_SAMPLE_FRAMES);
    Serial.printf("    flush chunks      : %lu  (%.1f per frame)\n",
                  (unsigned long)s.flushes, (double)s.flushes / DIAG_FPS_SAMPLE_FRAMES);
    Serial.printf("    pixels pushed     : %llu\n", (unsigned long long)s.total_px);
    Serial.printf("    time in flush     : %llu us\n", (unsigned long long)s.total_us);
    Serial.printf("    slowest chunk     : %lu us\n", (unsigned long)s.max_us);
    Serial.printf("    >> bus throughput : %.2f MB/s\n", mbps);
    Serial.printf("    >> full-frame fps : %.1f  (flush only)\n", fps_bus);
    Serial.printf("    >> end-to-end fps : %.1f  (render + flush)\n", fps_real);
    Serial.println("    NOTE: end-to-end fps is what decides whether the page-slide");
    Serial.println("          animation is viable. Below ~20 we swap it for a fade.");
    Serial.println(LINE);
}

/* ==========================================================================
 * MEASUREMENT 3 - LVGL heap
 * ======================================================================= */
void diag_lvgl_heap_report(void)
{
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    const bsp_status_t *st = bsp_status();

    Serial.println("[3] MEMORY");
    Serial.printf("    LV_MEM_SIZE       : %lu bytes (configured)\n",
                  (unsigned long)m.total_size);
    Serial.printf("    LVGL used now     : %lu bytes (%u%%)\n",
                  (unsigned long)(m.total_size - m.free_size), m.used_pct);
    Serial.printf("    LVGL max used     : %lu bytes  <-- high-water\n",
                  (unsigned long)m.max_used);
    Serial.printf("    LVGL frag         : %u%%\n", m.frag_pct);
    Serial.printf("    draw buffer       : %lu bytes (%d lines, INTERNAL SRAM)\n",
                  (unsigned long)st->draw_buf_bytes, LV_DRAW_BUF_LINES);
    Serial.printf("    internal SRAM free: %lu bytes\n",
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.printf("    PSRAM used        : %lu bytes (design target: 0 in v1)\n",
                  (unsigned long)(ESP.getPsramSize() - ESP.getFreePsram()));
    Serial.println(LINE);
}

/* ==========================================================================
 * MEASUREMENT 4 - QMI8658 raw axes
 *
 * Register map below is from the QMI8658 part datasheet, not from the
 * verified board facts. It is self-checked: WHO_AM_I must read 0x05. If it
 * does not, we say the assumption is wrong instead of printing noise.
 * ======================================================================= */
#define QMI_REG_WHOAMI   0x00
#define QMI_REG_REVISION 0x01
#define QMI_REG_CTRL1    0x02
#define QMI_REG_CTRL2    0x03
#define QMI_REG_CTRL7    0x08
#define QMI_REG_AX_L     0x35
#define QMI_WHOAMI_VALUE 0x05

static bool s_imu_ok = false;
static bool s_imu_stream = false;   /* off by default: at 10Hz it buries the
                                     * touch output. Press 'i' to stream. */

bool diag_imu_stream_enabled(void) { return s_imu_stream; }

/* --- guided axis capture -------------------------------------------------
 * Three orientations are enough to pin down the full mapping, and three
 * unambiguous instructions beat six fiddly ones.
 *
 * Screen convention (matches LVGL): +X right, +Y DOWN, +Z INTO the screen
 * (away from the viewer). The mapping is chosen so that a mapped axis reads
 * +1g when gravity points that way - i.e. a maze ball rolls downhill with no
 * extra sign juggling later, and a flat screen-up board reads z = +1g.
 */
static int  s_cap_step = 0;            /* 0 = idle, 1..3 = awaiting a pose */
static int8_t s_cap_src[3];            /* [screenX, screenY, screenZ] */
static int8_t s_cap_sign[3];

static const char *s_cap_prompt[3] = {
    "1/3  Lay the board FLAT on the desk, SCREEN FACING UP.",
    "2/3  Hold the board UPRIGHT, facing you, with the USB port at the BOTTOM.",
    "3/3  From that upright pose, tilt it so the RIGHT EDGE of the screen\n"
    "     points at the floor."
};
/* which screen axis each pose resolves: pose1 -> Z, pose2 -> Y, pose3 -> X */
static const int s_cap_axis[3] = { 2, 1, 0 };
static const char *s_axis_name[3] = { "X (right)", "Y (down)", "Z (into screen)" };
static const char *s_raw_name[3]  = { "ax", "ay", "az" };

static bool imu_read_raw(int16_t *ax, int16_t *ay, int16_t *az);
#if BSP_IMU_AXES_VERIFIED
static void imu_to_screen(int16_t ax, int16_t ay, int16_t az,
                          float *sx, float *sy, float *sz);
#endif

void diag_imu_capture_start(void)
{
    if (!s_imu_ok) {
        Serial.println("IMU not initialised - cannot capture.");
        return;
    }
    s_cap_step = 1;
    Serial.println();
    Serial.println("=== GUIDED IMU AXIS CAPTURE ===============================");
    Serial.println("Hold each pose steadily, then press ENTER (or any key).");
    Serial.println();
    Serial.println(s_cap_prompt[0]);
}

/* Averages samples, then reports the dominant raw axis and its sign. */
static void cap_take(void)
{
    long sx = 0, sy = 0, sz = 0; int n = 0;
    for (int i = 0; i < 24; i++) {
        int16_t a, b, c;
        if (imu_read_raw(&a, &b, &c)) { sx += a; sy += b; sz += c; n++; }
        delay(12);
    }
    if (n == 0) { Serial.println("    read failed"); return; }

    const long v[3] = { sx / n, sy / n, sz / n };
    int dom = 0;
    for (int i = 1; i < 3; i++) if (labs(v[i]) > labs(v[dom])) dom = i;

    const int screen_axis = s_cap_axis[s_cap_step - 1];
    s_cap_src[screen_axis]  = (int8_t)dom;
    /* mapped value must read +1g when gravity points along this screen axis */
    s_cap_sign[screen_axis] = (v[dom] > 0) ? 1 : -1;

    Serial.printf("    avg ax=%6ld ay=%6ld az=%6ld  -> dominant %s (%+.2fg)\n",
                  v[0], v[1], v[2], s_raw_name[dom], v[dom] / 8192.0f);
    Serial.printf("    screen %s  =  %s%s\n", s_axis_name[screen_axis],
                  s_cap_sign[screen_axis] < 0 ? "-" : "+", s_raw_name[dom]);

    /* Sanity: the dominant axis should be a clear ~1g, not an ambiguous tilt */
    if (labs(v[dom]) < 6000)
        Serial.println("    !! weak reading (<0.73g) - hold the pose more squarely and redo");

    s_cap_step++;
    if (s_cap_step <= 3) {
        Serial.println();
        Serial.println(s_cap_prompt[s_cap_step - 1]);
        return;
    }

    /* --- done: check the three axes are distinct, then emit the defines --- */
    s_cap_step = 0;
    Serial.println();
    const bool distinct = (s_cap_src[0] != s_cap_src[1]) &&
                          (s_cap_src[1] != s_cap_src[2]) &&
                          (s_cap_src[0] != s_cap_src[2]);
    if (!distinct) {
        Serial.println("!! Two poses resolved to the SAME raw axis, so the mapping is");
        Serial.println("   not valid. That usually means a pose was held off-square.");
        Serial.println("   Press 'x' and redo.");
        Serial.println(LINE);
        return;
    }

    Serial.println("=== RESULT - paste into include/board_pins.h ==============");
    Serial.println("#define BSP_IMU_AXES_VERIFIED 1");
    Serial.println("#if BSP_IMU_AXES_VERIFIED");
    for (int i = 0; i < 3; i++) {
        const char n = (i == 0) ? 'X' : (i == 1) ? 'Y' : 'Z';
        Serial.printf("  #define BSP_IMU_%c_SRC    %d    /* %s */\n",
                      n, s_cap_src[i], s_raw_name[s_cap_src[i]]);
        Serial.printf("  #define BSP_IMU_%c_SIGN   %d\n", n, s_cap_sign[i]);
    }
    Serial.println("#endif");
    Serial.println("===========================================================");
    Serial.println("Convention: +X right, +Y DOWN, +Z INTO the screen (away from");
    Serial.println("the viewer); each reads +1g when gravity points that way, so a");
    Serial.println("board lying flat screen-up reads z = +1g.");
    Serial.println(LINE);
}


void diag_imu_begin(void)
{
    Serial.println("[4] IMU (QMI8658) AXIS MAPPING");

    uint8_t who = 0, rev = 0;
    if (!bsp_i2c_read(I2C_ADDR_IMU, QMI_REG_WHOAMI, &who, 1)) {
        Serial.println("    FAILED: no response at 0x6B");
        Serial.println(LINE);
        return;
    }
    bsp_i2c_read(I2C_ADDR_IMU, QMI_REG_REVISION, &rev, 1);
    Serial.printf("    WHO_AM_I = 0x%02X (expect 0x%02X), REVISION = 0x%02X\n",
                  who, QMI_WHOAMI_VALUE, rev);

    if (who != QMI_WHOAMI_VALUE) {
        Serial.println("    !! Unexpected WHO_AM_I. The register map assumed here is");
        Serial.println("       wrong for this part. NOT reading axes - that would only");
        Serial.println("       print convincing garbage. Report this value back.");
        Serial.println(LINE);
        return;
    }

    /* CTRL1: address auto-increment for burst reads.
     * CTRL2: accelerometer +/-4g, 250 Hz ODR.
     * CTRL7: enable accelerometer only (gyro not needed for tilt/orientation). */
    bsp_i2c_write8(I2C_ADDR_IMU, QMI_REG_CTRL1, 0x60);
    bsp_i2c_write8(I2C_ADDR_IMU, QMI_REG_CTRL2, 0x15);
    bsp_i2c_write8(I2C_ADDR_IMU, QMI_REG_CTRL7, 0x01);
    delay(50);
    s_imu_ok = true;

    Serial.println("    accelerometer enabled (+/-4g, 250Hz, 8192 LSB/g)");
#if BSP_IMU_AXES_VERIFIED
    Serial.println("    axis mapping VERIFIED: screen = -raw on all three axes");
    Serial.println("      +X right, +Y down, +Z into screen");
    {
        int16_t ax, ay, az; float sx, sy, sz;
        if (imu_read_raw(&ax, &ay, &az)) {
            imu_to_screen(ax, ay, az, &sx, &sy, &sz);
            Serial.printf("      now: SCREEN x=%+5.2f y=%+5.2f z=%+5.2f\n", sx, sy, sz);
            Serial.println("      (flat on desk screen-up should read x~0 y~0 z~+1.00)");
        }
    }
#else
    Serial.println("    axis mapping NOT yet verified - press 'x' to capture it");
#endif
    Serial.println("    Press 'i' to start/stop the 10 Hz axis stream.");
    Serial.println("    HOLD THE BOARD IN EACH OF THESE AND NOTE WHICH AXIS READS +/-1g:");
    Serial.println("      1. flat on desk, screen UP");
    Serial.println("      2. flat on desk, screen DOWN");
    Serial.println("      3. upright, USB port DOWN");
    Serial.println("      4. upright, USB port UP");
    Serial.println("      5. upright, rotated LEFT 90 deg");
    Serial.println("      6. upright, rotated RIGHT 90 deg");
    Serial.println(LINE);
}

static bool imu_read_raw(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t d[6];
    if (!bsp_i2c_read(I2C_ADDR_IMU, QMI_REG_AX_L, d, sizeof(d))) return false;
    *ax = (int16_t)((d[1] << 8) | d[0]);
    *ay = (int16_t)((d[3] << 8) | d[2]);
    *az = (int16_t)((d[5] << 8) | d[4]);
    return true;
}

#if BSP_IMU_AXES_VERIFIED
/* Applies the verified mapping. Owned by imu.cpp from Phase 2 onward; it
 * lives here for now purely so measurement 4 can verify itself. */
static void imu_to_screen(int16_t ax, int16_t ay, int16_t az,
                          float *sx, float *sy, float *sz)
{
    const int16_t raw[3] = { ax, ay, az };
    *sx = BSP_IMU_X_SIGN * raw[BSP_IMU_X_SRC] / 8192.0f;
    *sy = BSP_IMU_Y_SIGN * raw[BSP_IMU_Y_SRC] / 8192.0f;
    *sz = BSP_IMU_Z_SIGN * raw[BSP_IMU_Z_SRC] / 8192.0f;
}
#endif

/* Exported for the tilt maze. Uses the SAME frozen raw->screen mapping as
 * the Phase 1 measurement - the maze must never carry its own copy of the
 * axis signs, which is exactly how a second, subtly different mapping gets
 * introduced. */
bool diag_imu_read_screen(float *x, float *y, float *z)
{
#if BSP_IMU_AXES_VERIFIED
    int16_t ax, ay, az;
    if (!s_imu_ok || !imu_read_raw(&ax, &ay, &az)) return false;
    imu_to_screen(ax, ay, az, x, y, z);
    return true;
#else
    (void)x; (void)y; (void)z;
    return false;
#endif
}

void diag_imu_tick(void)
{
    if (!s_imu_ok || !s_imu_stream) return;
    int16_t ax, ay, az;
    if (!imu_read_raw(&ax, &ay, &az)) return;
    /* +/-4g full scale over a signed 16-bit range -> 8192 LSB per g */
#if BSP_IMU_AXES_VERIFIED
    float sx, sy, sz;
    imu_to_screen(ax, ay, az, &sx, &sy, &sz);
    Serial.printf("IMU raw ax=%6d ay=%6d az=%6d | SCREEN x=%+5.2f y=%+5.2f z=%+5.2f\n",
                  ax, ay, az, sx, sy, sz);
#else
    Serial.printf("IMU  ax=%6d ay=%6d az=%6d   (g: %+5.2f %+5.2f %+5.2f)\n",
                  ax, ay, az, ax / 8192.0f, ay / 8192.0f, az / 8192.0f);
#endif
}

/* ==========================================================================
 * MEASUREMENT 5 - BOOT button
 *
 * GPIO0 is OBSERVED, never asserted to be the button. It is configured as
 * INPUT_PULLUP only: never driven, no pulldown, never held low across a
 * reset. That keeps BOOT+RESET bootloader entry working.
 * ======================================================================= */
static int  s_boot_last = -1;
static bool s_boot_seen_press = false;

void diag_boot_tick(void)
{
    const int v = digitalRead(BSP_BOOT_CANDIDATE);
    if (s_boot_last < 0) { s_boot_last = v; return; }
    if (v != s_boot_last) {
        Serial.printf("GPIO%d -> %s\n", BSP_BOOT_CANDIDATE, v ? "HIGH (released)" : "LOW (pressed)");
        if (v == 0 && !s_boot_seen_press) {
            s_boot_seen_press = true;
            Serial.println(">>> GPIO0 went LOW. If you just pressed BOOT, that");
            Serial.println("    confirms BSP_BOOT_BTN = 0. Report it back.");
        }
        s_boot_last = v;
    }
}

/* ==========================================================================
 * MEASUREMENT 6 - brightness
 *
 * Arduino_GFX 1.6.7 DOES expose Arduino_CO5300::setBrightness(), which
 * issues command 0x51 (Write Display Brightness Value, Normal Mode). So the
 * software path is confirmed to exist. What remains unknown is whether this
 * panel visibly honours it - that is what this sweep is for.
 * ======================================================================= */
void diag_brightness_sweep(void)
{
    Serial.println("[6] BRIGHTNESS (CO5300 cmd 0x51 via setBrightness)");
    if (!bsp_display_available()) {
        Serial.println("    BLOCKED: display not brought up (QSPI pins unresolved).");
        Serial.println(LINE);
        return;
    }
    Serial.println("    Sweeping. WATCH THE SCREEN and report whether it changes:");
    const uint8_t steps[] = { 0xFF, 0xC0, 0x80, 0x40, 0x10, 0x00, BRIGHT_FULL };
    for (uint8_t i = 0; i < sizeof(steps); i++) {
        Serial.printf("      level 0x%02X\n", steps[i]);
        bsp_set_brightness(steps[i]);
        delay(700);
    }
    Serial.println("    restored to default");
    Serial.println(LINE);
}

/* ==========================================================================
 * MEASUREMENT 7 - PCF85063 RTC and its backup power
 *
 * Register map is the standard PCF85063A map (a property of the part). It
 * is plausibility-checked: if the BCD fields decode to nonsense we say so
 * rather than reporting a bogus time.
 *
 * The OS bit (bit 7 of the seconds register) is set by the part when its
 * oscillator has stopped - i.e. when it lost power. That is exactly the
 * signal that tells us whether a backup cell is fitted.
 * ======================================================================= */
#define PCF_REG_CTRL1   0x00
#define PCF_REG_SECONDS 0x04
#define PCF_CTRL1_STOP  0x20   /* bit 5: stop the RTC time circuits */
#define PCF_SEC_OS      0x80   /* bit 7 of seconds: oscillator-stop flag */

static uint8_t bcd2dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }

void diag_rtc_report(void)
{
    Serial.println("[7] RTC (PCF85063) + BACKUP POWER");

    uint8_t d[7];
    if (!bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_SECONDS, d, sizeof(d))) {
        Serial.println("    FAILED: no response at 0x51");
        Serial.println(LINE);
        return;
    }

    const bool os_flag = (d[0] & 0x80) != 0;
    const uint8_t sec  = bcd2dec(d[0] & 0x7F);
    const uint8_t min  = bcd2dec(d[1] & 0x7F);
    const uint8_t hour = bcd2dec(d[2] & 0x3F);
    const uint8_t day  = bcd2dec(d[3] & 0x3F);
    const uint8_t mon  = bcd2dec(d[5] & 0x1F);
    const uint16_t yr  = (uint16_t)(2000 + bcd2dec(d[6]));

    const bool plausible = sec < 60 && min < 60 && hour < 24 &&
                           day >= 1 && day <= 31 && mon >= 1 && mon <= 12;

    Serial.printf("    raw: %02X %02X %02X %02X %02X %02X %02X\n",
                  d[0], d[1], d[2], d[3], d[4], d[5], d[6]);
    if (plausible)
        Serial.printf("    time: %04u-%02u-%02u %02u:%02u:%02u\n", yr, mon, day, hour, min, sec);
    else
        Serial.println("    time: IMPLAUSIBLE - assumed register map may be wrong");

    Serial.printf("    OS (oscillator-stop) flag: %s\n",
                  os_flag ? "SET  -> RTC lost power since it was last set"
                          : "clear -> RTC has kept running");
    Serial.println();
    Serial.println("    POWER-CYCLE TEST (this is measurement 7, and it needs you):");
    Serial.println("      a) note the time printed above");
    Serial.println("      b) unplug USB completely for 60 seconds");
    Serial.println("         (no battery attached; repeat with battery if one is fitted)");
    Serial.println("      c) plug back in and compare");
    Serial.println("      If the time advanced correctly and OS stays clear, a backup");
    Serial.println("      cell is fitted. If the time reset and OS is SET, there is no");
    Serial.println("      backup - and 'age advances across power-off' is unmet by the");
    Serial.println("      HARDWARE. That changes the product, so report it before Phase 5.");
    Serial.println(LINE);
}

/* ==========================================================================
 * MEASUREMENT 7b - is the OS flag actually clearable?
 *
 * The datasheet expectation is that OS is a STICKY "clock integrity not
 * guaranteed" flag: hardware sets it when the oscillator stops, and only
 * software clears it, by writing bit 7 = 0 to the seconds register. That
 * would mean it stays SET forever until we clear it, which is exactly what
 * was observed across the power-cycle test.
 *
 * That is an expectation, not a verified fact about this part, so this
 * routine TESTS it rather than assuming it. Two methods are tried:
 *   A) write seconds back with bit 7 clear, oscillator left running
 *   B) same, bracketed by the CTRL1 STOP bit
 *
 * Writing the seconds register back with its own value is benign; worst
 * case the clock shifts by under a second, and the clock is not yet set.
 * ======================================================================= */
void diag_rtc_clear_os(void)
{
    Serial.println("[7b] PCF85063 OS-FLAG CLEAR TEST");

    uint8_t sec = 0;
    if (!bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_SECONDS, &sec, 1)) {
        Serial.println("    FAILED: cannot read seconds register");
        Serial.println(LINE);
        return;
    }
    Serial.printf("    initial seconds reg = 0x%02X  (OS = %s)\n",
                  sec, (sec & PCF_SEC_OS) ? "SET" : "clear");

    if (!(sec & PCF_SEC_OS)) {
        Serial.println("    OS is already clear - nothing to test right now.");
        Serial.println(LINE);
        return;
    }

    /* --- method A: plain write, oscillator running ----------------------- */
    bsp_i2c_write8(I2C_ADDR_RTC, PCF_REG_SECONDS, (uint8_t)(sec & 0x7F));
    delay(150);
    uint8_t a = 0;
    bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_SECONDS, &a, 1);
    Serial.printf("    method A (plain write)      -> 0x%02X  OS = %s\n",
                  a, (a & PCF_SEC_OS) ? "STILL SET" : "CLEARED");

    if (!(a & PCF_SEC_OS)) {
        Serial.println();
        Serial.println("    RESULT: OS clears with a plain seconds write.");
        Serial.println("    => set BSP_RTC_OS_CLEARABLE to 1.");
        Serial.println("    => Phase 5 rtc_set() must clear OS after writing the time,");
        Serial.println("       otherwise the flag stays SET forever and tells us nothing.");
        Serial.println(LINE);
        return;
    }

    /* --- method B: bracket the write with the CTRL1 STOP bit ------------- */
    uint8_t c1 = 0;
    bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_CTRL1, &c1, 1);
    Serial.printf("    CTRL1 = 0x%02X, retrying with STOP bracket\n", c1);

    bsp_i2c_write8(I2C_ADDR_RTC, PCF_REG_CTRL1, (uint8_t)(c1 | PCF_CTRL1_STOP));
    bsp_i2c_read (I2C_ADDR_RTC, PCF_REG_SECONDS, &sec, 1);
    bsp_i2c_write8(I2C_ADDR_RTC, PCF_REG_SECONDS, (uint8_t)(sec & 0x7F));
    bsp_i2c_write8(I2C_ADDR_RTC, PCF_REG_CTRL1, (uint8_t)(c1 & ~PCF_CTRL1_STOP));
    delay(250);

    uint8_t b = 0;
    bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_SECONDS, &b, 1);
    Serial.printf("    method B (STOP bracketed)   -> 0x%02X  OS = %s\n",
                  b, (b & PCF_SEC_OS) ? "STILL SET" : "CLEARED");

    Serial.println();
    if (!(b & PCF_SEC_OS)) {
        Serial.println("    RESULT: OS clears only with the STOP bracket.");
        Serial.println("    => set BSP_RTC_OS_CLEARABLE to 1 and use the bracketed");
        Serial.println("       write in Phase 5 rtc_set().");
    } else {
        Serial.println("    RESULT: OS did NOT clear by either method.");
        Serial.println("    => the flag is unusable as an integrity signal on this part.");
        Serial.println("       Phase 5 must fall back to the numeric plausibility checks");
        Serial.println("       alone (range + jump detection). Report this back.");
    }
    Serial.println(LINE);
}

/* ==========================================================================
 * Storage foundation self-test
 * ======================================================================= */
void diag_storage_report(void)
{
    Serial.println("[S] STORAGE FOUNDATION");
    Serial.printf("    sizeof(save_t)    : %u bytes (budget %u)\n",
                  (unsigned)sizeof(save_t), (unsigned)SAVE_SIZE_BUDGET);
    Serial.printf("    schema version    : %u\n", SAVE_SCHEMA_VERSION);
    Serial.printf("    journal entries   : %u x %u bytes\n",
                  (unsigned)(sizeof(((save_t*)0)->journal) / sizeof(journal_entry_t)),
                  (unsigned)sizeof(journal_entry_t));

    save_t s;
    const load_result_t r = storage_load(&s);
    Serial.printf("    load result       : %s\n", storage_load_result_str(r));

    /* NON-DESTRUCTIVE FROM MILESTONE 6 ONWARDS.
     * This round-trip writes test values into the ONE real save slot. That
     * was harmless in Phase 1, when nothing real was stored - but the slot
     * now holds the actual Visitor, and this self-test runs on every boot
     * BEFORE persist_load(). It was overwriting hunger/happiness/weight with
     * 42.5/77.0/63.4 every single boot, which is exactly the "stale values"
     * mismatch seen in the fidelity test.
     *
     * Snapshot the real save first and put it back afterwards, so the test
     * still exercises the genuine write path without eating the pet. */
    save_t orig;
    const bool orig_valid = (r == LOAD_OK || r == LOAD_MIGRATED);
    if (orig_valid) memcpy(&orig, &s, sizeof(orig));

    /* Round-trip test: write, read back, compare. Proves CRC, sizing and
     * the shadow-compare all agree before any game state depends on them. */
    s.hunger = 42.5f; s.happiness = 77.0f; s.weight_g = 63.4f;
    s.days_alive_max = 3;
    const bool w1 = storage_save(&s, true);

    save_t back;
    const load_result_t r2 = storage_load(&back);
    const bool match = (r2 == LOAD_OK) &&
                       (back.hunger == 42.5f) &&
                       (back.happiness == 77.0f) &&
                       (back.days_alive_max == 3);

    Serial.printf("    write             : %s\n", w1 ? "ok" : "FAILED");
    Serial.printf("    read-back         : %s (%s)\n",
                  match ? "MATCH" : "MISMATCH", storage_load_result_str(r2));

    /* Shadow compare: an identical second write must not touch flash. */
    const uint32_t before = storage_write_count();
    storage_save(&back, true);
    const bool skipped = (storage_write_count() == before);
    Serial.printf("    shadow-compare    : %s (identical blob %s flash)\n",
                  skipped ? "ok" : "NOT WORKING",
                  skipped ? "skipped" : "REWROTE");

    /* Corruption handling: flip a byte and confirm we get a safe reset, not
     * a brick. Done on a copy in RAM, so nothing on flash is damaged. */
    save_t bad; memcpy(&bad, &back, sizeof(bad));
    ((uint8_t *)&bad)[sizeof(save_t) / 2] ^= 0xFF;
    const uint32_t recrc = storage_crc32((const uint8_t *)&bad + 8, sizeof(save_t) - 8);
    Serial.printf("    crc detects damage: %s\n",
                  (recrc != bad.crc32) ? "yes" : "NO - CRC IS NOT COVERING THE STRUCT");

    /* Put the real save back exactly as it was. */
    if (orig_valid) {
        const bool restored = storage_save(&orig, true);
        Serial.printf("    real save         : %s\n",
                      restored ? "preserved (test was non-destructive)"
                               : "RESTORE FAILED");
    } else {
        storage_wipe();      /* it was empty before; leave it empty */
        Serial.println("    real save         : none existed, left empty");
    }

    Serial.printf("    writes this boot  : %lu (skipped %lu)\n",
                  (unsigned long)storage_write_count(),
                  (unsigned long)storage_skipped_count());
    Serial.println(LINE);
}

/* ==========================================================================
 * Summary
 * ======================================================================= */
void diag_summary(void)
{
    const bsp_status_t *st = bsp_status();
    Serial.println("PHASE 1 STATUS");
    Serial.printf("  I2C bus          : %s\n", st->i2c_ok      ? "OK" : "FAIL");
    Serial.printf("  TCA9554 sequence : %s\n", st->ioexp_ok    ? "OK" : "FAIL");
    Serial.printf("  CST820 touch     : %s\n", st->touch_ok    ? "OK (auto-sleep disabled)" : "FAIL");
    Serial.printf("  LVGL             : %s\n", st->lvgl_ok     ? "OK" : "FAIL");
    Serial.printf("  CO5300 display   : %s\n", st->display_ok  ? "OK"
                                              : "BLOCKED - QSPI pins unresolved");
    Serial.println();

    if (!st->display_ok) {
        Serial.println("  >> TO UNBLOCK THE DISPLAY, I need six numbers:");
        Serial.println("       QSPI CS, SCK, D0, D1, D2, D3");
        Serial.println("     from the Waveshare wiki/demo for the **V2** board");
        Serial.println("     (its display section must say CO5300, not SH8601).");
        Serial.println("     Put them in include/board_pins.h and set");
        Serial.println("     BSP_QSPI_VERIFIED to 1.");
        Serial.println();
    }
    Serial.println("  Touch the screen to print live touch coordinates.");
    Serial.println("  Press BOOT to confirm the button GPIO.");
    Serial.println(LINE);
}

/* ==========================================================================
 * Interactive serial console
 * ======================================================================= */

/* ==========================================================================
 * PHASE 2 TEST HOOKS
 *
 * Every animation, every face style and the whole bubble policy has to be
 * triggerable on demand - an animation you can only observe by waiting for
 * it is an animation you cannot actually verify.
 * ======================================================================= */

static int s_eye_cycle = 0, s_mouth_cycle = 0, s_live_cycle = 0;

static void diag_pet_anim(pet_anim_t a)
{
    ui_pet_play(a);
    Serial.printf("PET anim -> %s\n", ui_pet_anim_name(a));
}

static void diag_pet_eyes(void)
{
    s_eye_cycle = (s_eye_cycle + 1) % EYE_STYLE_COUNT;
    ui_pet_set_face(s_eye_cycle, -1, -1);
    Serial.printf("PET eyes -> %s  (%d/%d)\n",
                  ui_pet_eye_name(s_eye_cycle), s_eye_cycle + 1, EYE_STYLE_COUNT);
}

static void diag_pet_mouth(void)
{
    s_mouth_cycle = (s_mouth_cycle + 1) % MOUTH_STYLE_COUNT;
    ui_pet_set_face(-1, s_mouth_cycle, -1);
    Serial.printf("PET mouth -> %s  (%d/%d)\n",
                  ui_pet_mouth_name(s_mouth_cycle), s_mouth_cycle + 1, MOUTH_STYLE_COUNT);
}

/* Live modifiers now come from real pet state every second, so this sets the
 * underlying WEIGHT rather than the transient render value - otherwise the
 * next sim tick would immediately overwrite it. */
static void diag_pet_live(void)
{
    static const float W[4] = { PET_WEIGHT_START_G, PET_WEIGHT_MAX_G,
                                PET_WEIGHT_MIN_G,  PET_WEIGHT_START_G };
    s_live_cycle = (s_live_cycle + 1) % 4;
    pet_mutable()->weight_g = W[s_live_cycle];
    Serial.printf("PET weight -> %.1f g (norm %.2f)\n",
                  (double)W[s_live_cycle], (double)pet_weight_norm());
}

static void diag_bubble_one(void)
{
    static int tier = 0;
    const bubble_tier_t t = (bubble_tier_t)(tier % BUBBLE_TIER_COUNT);
    tier++;
    ui_bubble_say(t, strings_random(t));
}

/* The stress test. NOTE: most of these SHOULD be refused - that is the
 * feature under test, not a failure. What must hold is that the refusals are
 * the CORRECT ones, that a higher tier always preempts a lower one, and that
 * exactly one bubble is ever on screen. */
static void diag_bubble_spam(void)
{
    static const uint8_t seq[20] = { 3,3,3,2,2,1,1,0,0,3,2,1,0,3,3,2,1,0,2,3 };
    uint32_t a0, s0, a1, s1;
    ui_bubble_stats(&a0, &s0);

    Serial.println();
    Serial.println("=== BUBBLE STRESS TEST: 20 requests, mixed tiers ==========");
    Serial.println("Most SHOULD be suppressed. Watch the screen: exactly one");
    Serial.println("bubble at a time, no stacking, no flicker.");
    Serial.println();

    for (int i = 0; i < 20; i++) {
        const bubble_tier_t t = (bubble_tier_t)seq[i];
        Serial.printf("  [%2d] ", i + 1);
        ui_bubble_say(t, strings_random(t));
    }

    ui_bubble_stats(&a1, &s1);
    Serial.println();
    Serial.printf("  accepted %lu, suppressed %lu, of 20 requests\n",
                  (unsigned long)(a1 - a0), (unsigned long)(s1 - s0));
    Serial.println("  A low accept count here is CORRECT behaviour.");
    Serial.println(LINE);
}

/* Bubble layout test. Runs the four required strings at three pet positions
 * - hard left, centre, hard right - because the clamping bug only shows when
 * the pet is near an edge, and a centred test would have passed while the
 * real failure sat one wander away. */
static const char *LAYOUT_STRINGS[4] = {
    "HEY!",
    "Hi!",
    "Is it snack time yet?",
    "WHY AM I UPSIDE DOWN?!"
};

static void diag_bubble_layout(void)
{
    const lv_coord_t LEFT   = PET_WALK_MARGIN_PX;
    const lv_coord_t CENTRE = PET_HOME_X;
    const lv_coord_t RIGHT  = BSP_LCD_W - PET_BOX_PX - PET_WALK_MARGIN_PX;
    const struct { lv_coord_t x; const char *name; } POS[3] = {
        { LEFT, "pet at LEFT edge" },
        { CENTRE, "pet CENTRED" },
        { RIGHT, "pet at RIGHT edge" },
    };

    Serial.println();
    Serial.println("=== BUBBLE LAYOUT TEST ====================================");
    Serial.printf("panel %dx%d, text wrap width %d, max box %d\n",
                  BSP_LCD_W, BSP_LCD_H, BUBBLE_TEXT_MAX_W, BUBBLE_BOX_MAX_W);

    for (int p = 0; p < 3; p++) {
        ui_pet_set_x(POS[p].x);
        Serial.printf("\n  --- %s (x=%d) ---\n", POS[p].name, (int)POS[p].x);
        for (int i = 0; i < 4; i++) {
            ui_bubble_test_show(LAYOUT_STRINGS[i]);
            /* let the panel actually show each one */
            for (int k = 0; k < 18; k++) { lv_timer_handler(); delay(50); }
        }
    }
    ui_pet_set_x(PET_HOME_X);
    Serial.println();
    Serial.println("  Every line above must read IN BOUNDS.");
    Serial.println(LINE);
}

static void diag_menu_report(void)
{
    const pet_state_t *p = pet_get();
    Serial.println();
    Serial.println("=== PAGER / PET STATE =====================================");
    Serial.printf("  menu       : %s", menu_is_open() ? "OPEN" : "closed");
    if (menu_is_open()) Serial.printf("  page %u %s", menu_page(), page_name(menu_page()));
    Serial.println();
    Serial.printf("  transition : %s\n", menu_transition_name());
    Serial.printf("  gestures   : metrics %s   tap<=%dms/<=%dpx   swipe>=%dpx & |dx|>%d|dy|\n",
                  menu_metrics() ? "ON" : "off", TAP_MAX_MS, TAP_MAX_TRAVEL_PX,
                  SWIPE_MIN_TRAVEL_PX, SWIPE_AXIS_RATIO);
    Serial.printf("  bubbles    : %s\n", ui_bubble_suppressed() ? "SUPPRESSED" : "active");
    Serial.printf("  stats      : hun %.0f  hap %.0f  dis %.0f  cln %.0f  eng %.0f\n",
                  p->hunger, p->happiness, p->discipline, p->cleanliness, p->energy);
    Serial.printf("  body       : %s day %d, %.1f g, mood %s\n",
                  pet_stage_name(p->stage), (int)p->days_alive, p->weight_g,
                  pet_mood_name(pet_mood()));
    Serial.printf("  bathroom   : %.0f%%  messes %u\n", p->bathroom, care_mess_count());
    Serial.printf("  pet        : anim %s  x=%d y=%d  %s  bath_phase %u\n",
                  ui_pet_anim_name(ui_pet_current()), (int)ui_pet_x(), (int)ui_pet_y(),
                  ui_pet_hidden() ? "HIDDEN" : "visible", ui_pet_bath_phase());
    Serial.println(LINE);
}

/* Walk all six pages twice, so the 6->1 and 1->6 wraps are both exercised
 * and any leak from repeated build/delete shows up in the heap figure. */
static void diag_page_sweep(void)
{
    if (!menu_is_open()) menu_open();
    Serial.println();
    Serial.println("=== PAGE SWEEP: 12 steps forward (two full loops) =========");
    for (int i = 0; i < 12; i++) {
        menu_step(1);
        for (int k = 0; k < 10; k++) { lv_timer_handler(); delay(30); }
    }
    lv_mem_monitor_t m; lv_mem_monitor(&m);
    Serial.printf("  after sweep: lvgl used %lu, max %lu, frag %u%%\n",
                  (unsigned long)(m.total_size - m.free_size),
                  (unsigned long)m.max_used, m.frag_pct);
    Serial.println(LINE);
}

/* Set the clock to a fixed, plausible moment so the RTC path - including the
 * OS-flag clear - is exercisable without a date picker existing yet. */
static void diag_rtc_set_demo(void)
{
    rtc_time_t t = { 2026, 8, 28, 19, 30, 0 };
    Serial.println("RTC set -> 2026-08-28 19:30:00");
    if (!rtc_set(&t)) return;
    char b[32]; rtc_format(rtc_now(), b, sizeof(b));
    Serial.printf("RTC now: %s   health: %s\n", b, rtc_health_name(rtc_health()));

    /* Same baseline logic as the on-screen setter, so serial testing
     * exercises the real path rather than a shortcut. */
    pet_state_t *p = pet_mutable();
    if (!p->hatch_ts) {
        p->hatch_ts = rtc_now();
        p->days_alive = 0;
        pet_apply_stage_for_day(0);
        Serial.println("hatch baseline established, age 0");
    }
    p->last_sim_ts = rtc_now();
    persist_save(true);
}

/* Simulate having been switched off for N hours, using the REAL catch-up
 * path - not a separate test implementation, which would prove nothing. */
static void diag_simulate_absence(uint32_t hours)
{
    const uint32_t now = rtc_trusted() ? rtc_now() : 1788000000UL;
    const uint32_t then = now - hours * 3600UL;
    Serial.printf("\nSIMULATING an absence of %lu hours\n", (unsigned long)hours);
    sim_report_t rep;
    sim_catch_up(then, now, &rep);
    sim_print_report();
    const char *g = sim_return_greeting(&rep);
    Serial.printf("  return greeting: %s\n", g ? g : "(none - too short to mention)");
    if (g) ui_bubble_say(BUBBLE_T0_CRITICAL, g);
    /* No dream hook needed here: sim_catch_up() ran care_advance() for every
     * chunk, so any sleep period the absence contained has already opened,
     * accumulated and closed itself - recording its one dream if it earned
     * one. This just reports what that produced. */
    const pet_state_t *q = pet_get();
    if (q->pending_dream)
        Serial.printf("  dream from the absence: \"%s\" (waiting to be told)\n",
                      dialogue_dream_bubble((uint8_t)(q->pending_dream - 1)));
    else
        Serial.println("  dream from the absence: none earned");
}

/* Persistence fidelity. Freezes the simulator first, so any difference
 * between SAVED and RAW LOADED is a pack/save/load bug and nothing else. */
static void diag_persist_fidelity(void)
{
    pet_set_sim_suspended(true);

    care_reset();
    pet_state_t *p = pet_mutable();
    p->hunger      = 73.0f;
    p->happiness   = 61.0f;
    p->discipline  = 47.0f;
    p->cleanliness = 82.0f;
    p->energy      = 39.0f;
    p->weight_g    = 63.7f;
    p->bathroom    = 37.0f;
    care_set_lights(false);

    /* one known mess, at a known place */
    care_restore_mess(2 /*poop*/, 0, false, 0, 0, 120, 300);
    p->mess_count = care_mess_count();

    Serial.println();
    Serial.println("=== PERSISTENCE FIDELITY TEST =============================");
    Serial.println("simulation SUSPENDED - nothing else can move these values");
    persist_print_state("SAVED");
    persist_save(true);
    persist_report();
    Serial.println("Now reboot. The boot log prints [RAW LOADED] before any");
    Serial.println("catch-up runs, then [SIMULATED] after. SAVED must equal");
    Serial.println("RAW LOADED exactly.");
    Serial.println("-----------------------------------------------------------");
}

/* Jump the clock to a specific hour so the sleep window can be exercised on
 * demand instead of waiting until 8 pm. Establishes the hatch baseline if it
 * is missing, so the Visitor is a Baby rather than an Egg - naps are a Baby
 * behaviour and an Egg would not show them. */
static void diag_clock_to(uint8_t hour, uint8_t minute, const char *what)
{
    rtc_time_t t = { 2026, 8, 28, hour, minute, 0 };
    if (!rtc_set(&t)) { Serial.println("clock set FAILED"); return; }

    pet_state_t *p = pet_mutable();
    if (!p->hatch_ts) {
        p->hatch_ts = rtc_now();
        p->days_alive = 0;
        pet_apply_stage_for_day(0);
    }
    p->last_sim_ts = rtc_now();

    char b[32]; rtc_format(rtc_now(), b, sizeof(b));
    Serial.printf("CLOCK -> %s (%s), stage %s\n", b, what,
                  pet_stage_name(p->stage));
}

/* Seed a care HISTORY rather than poking the visible meters: evolution reads
 * the accumulators, so this is what a genuinely well- or badly-raised Visitor
 * actually looks like to the selection logic. */
/* engage = 100*games/(2*stage_days), so a fixed game count gives wildly
 * different engagement depending on how long the stage has run. Seed the
 * count from a TARGET engagement instead, or a mid-care test reads as
 * excellent at day 3 and neglectful at day 13. */
static void seed_engage(pet_state_t *p, float target)
{
    p->stage_start_day = 0;
    /* MUST be the same denominator evolve_scores() uses, not a copy of it.
     * This used to read days_alive + 0.5 - an integer cache, with no floor -
     * while evolve_scores() moved to a floored fractional age. On a Visitor
     * younger than half a day the two differed by 2x, so every seeded fixture
     * produced HALF the engagement it claimed and every cs derived from it
     * was low. Note games_played is an integer, so engagement is quantised at
     * 100/(2*stage_days) per game: seed on an older Visitor for fine steps. */
    const float stage_days = evolve_stage_days();
    float g = target * 2.0f * stage_days / 100.0f;
    if (g < 0.0f) g = 0.0f;
    p->games_played = (uint16_t)(g + 0.5f);
}

static void diag_seed_care(int level)   /* 0 poor, 1 mid, 2 excellent, 3 good */
{
    pet_state_t *p = pet_mutable();
    if (level == 3) {
        /* GOOD - the fourth fixture. The three original seeds gave the four
         * departure bands only three anchors, so 13-14 days had nothing to
         * test against.
         *
         * The values are not invented to taste: they are solved BACKWARDS
         * from the target. A 13-14 day departure needs stay01 near 0.73,
         * which needs 0.80*cs + 0.10*(ba+100) ~= 73. cs 75.9 with ba 24.3
         * gives 73.2. Everything below is then a plausible history that
         * produces those two numbers - attentive but not flawless: fed and
         * clean, played with often, one need missed, nothing unfair. */
        p->care_happy = 78.0f; p->care_fed = 80.0f; p->care_clean = 74.0f;
        p->care_sleep = 76.0f; p->care_discipline = 68.0f; p->nutrition = 22.0f;
        p->happiness = 80.0f; p->hunger = 65.0f; p->cleanliness = 76.0f;
        p->energy = 78.0f; p->discipline = 68.0f;
        seed_engage(p, 68.0f);
        p->meals = 10; p->junk_meals = 2;
        p->ignored_requests = 1; p->disc_unfair = 0; p->disc_correct = 2;
        p->weight_g = 55.0f;
        Serial.println("(test) seeded GOOD care history");
        evolve_explain();
        visit_report();
        return;
    }
    if (level == 1) {
        /* Middling: fed and clean enough, played with sometimes, discipline
         * about neutral, a couple of needs missed. The everyday case. */
        p->care_happy = 62.0f; p->care_fed = 65.0f; p->care_clean = 58.0f;
        p->care_sleep = 60.0f; p->care_discipline = 55.0f; p->nutrition = 35.0f;
        p->happiness = 65.0f; p->hunger = 55.0f; p->cleanliness = 60.0f;
        p->energy = 65.0f; p->discipline = 55.0f;
        seed_engage(p, 45.0f);
        p->meals = 10; p->junk_meals = 4;
        p->ignored_requests = 2; p->disc_unfair = 1; p->disc_correct = 1;
        p->weight_g = 58.0f;
        Serial.println("(test) seeded MID care history");
        evolve_explain();
        visit_report();
        return;
    }
    const bool good = (level == 2);
    if (good) {
        p->care_happy = 92.0f; p->care_fed = 95.0f; p->care_clean = 88.0f;
        p->care_sleep = 90.0f; p->care_discipline = 78.0f; p->nutrition = 10.0f;
        p->happiness = 95.0f; p->hunger = 70.0f; p->cleanliness = 90.0f;
        p->energy = 90.0f; p->discipline = 78.0f;
        seed_engage(p, 90.0f); p->meals = 10; p->junk_meals = 1;
        p->ignored_requests = 0; p->disc_unfair = 0; p->disc_correct = 3;
        p->weight_g = 52.0f;
    } else {
        p->care_happy = 30.0f; p->care_fed = 28.0f; p->care_clean = 25.0f;
        p->care_sleep = 35.0f; p->care_discipline = 30.0f; p->nutrition = 70.0f;
        p->happiness = 30.0f; p->hunger = 20.0f; p->cleanliness = 20.0f;
        p->discipline = 30.0f;
        seed_engage(p, 3.0f); p->meals = 8; p->junk_meals = 6;
        p->ignored_requests = 6; p->disc_unfair = 3; p->disc_correct = 0;
        p->weight_g = 78.0f;
    }
    Serial.printf("(test) seeded %s care history\n", good ? "EXCELLENT" : "POOR");
    evolve_explain();
    visit_report();
}

/* TIME TRAVEL. Moves the hatch timestamp BACKWARDS, which ages the Visitor
 * through the same derived path as real time - pet_age_days() reads the
 * clock, so nothing here is a special case. Advancing the RTC instead would
 * also shift the sleep window, which is not what we want to test. */
static void diag_time_travel(uint32_t hours)
{
    pet_state_t *p = pet_mutable();
    if (!p->hatch_ts) { Serial.println("no hatch timestamp - hatch it first"); return; }
    p->hatch_ts -= hours * 3600UL;
    pet_refresh_age();
    const float d = pet_age_days();
    Serial.printf("TIME TRAVEL +%luh  ->  age %.3f days (%s)\n",
                  (unsigned long)hours, (double)d, pet_stage_name(p->stage));
    if (pet_apply_stage_for_day(d) > 0) {
        const uint8_t f = evolve_pick_form(p->stage);
        if (f != p->form_id) evolve_present(f, false);
        evolve_on_stage_entered(p->stage, p->days_alive);
    }
    persist_mark_dirty("time travel");
}

static void diag_age_report(void)
{
    const pet_state_t *p = pet_get();
    Serial.println();
    Serial.println("=== AGE CLOCK =============================================");
    Serial.printf("  hatch_ts       : %lu\n", (unsigned long)p->hatch_ts);
    Serial.printf("  rtc trusted    : %s\n", rtc_trusted() ? "yes" : "NO -> age 0");
    Serial.printf("  pet_age_days() : %.4f  (fractional, derived from the clock)\n",
                  (double)pet_age_days());
    Serial.printf("  days_alive     : %u  (cache)   days_alive_max %u (monotonic)\n",
                  p->days_alive, p->days_alive_max);
    Serial.printf("  stage          : %s   boundaries %.1f / %.1f / %.1f\n",
                  pet_stage_name(p->stage), (double)STAGE_DAY_KID,
                  (double)STAGE_DAY_TEEN, (double)STAGE_DAY_ADULT);
    Serial.printf("  departure      : day %.2f %s   (window %.0f-%.0f)\n",
                  (double)visit_depart_day(),
                  visit_locked() ? "LOCKED" : "projected",
                  (double)VISIT_DEPART_MIN_DAY, (double)VISIT_DEPART_MAX_DAY);
    Serial.println("-----------------------------------------------------------");
}

static void diag_screen(bool pet)
{
    if (pet) { scr_main_show(); Serial.println("screen -> PET (Phase 2)"); }
    else     { ui_diag_show();  Serial.println("screen -> Phase 1 test card (frozen baseline)"); }
}

/* --- PHASE 9.5: identity, growth path, dreams, learned behaviour ---------
 * One place to check everything this pass persists. Written as a REPORT with
 * expectations rather than a dump, because the interesting questions here
 * are "did the Surprise survive the reboot" and "is the growth path complete"
 * - neither of which a list of raw bytes answers. */
void diag_identity_report(void)
{
    const pet_state_t *p = pet_get();
    Serial.println();
    Serial.println("=== IDENTITY / GROWTH / BEHAVIOUR =========================");

    Serial.printf("  colour  : palette %u", p->egg_color);
    if (p->egg_choice >= EGG_PALETTE_COUNT)
        Serial.printf("   choice SURPRISE%s\n",
                      p->stage == STAGE_EGG && !p->egg_hatch_ts
                        ? " (not resolved yet - shell shows the rainbow)"
                        : " (resolved and locked at START)");
    else
        Serial.printf("   choice %u (explicit)\n", p->egg_choice);

    Serial.printf("  gender  : %s   choice %s\n",
                  p->gender == GENDER_GIRL ? "girl" : "boy",
                  p->gender_choice == GENDER_BOY  ? "Boy" :
                  p->gender_choice == GENDER_GIRL ? "Girl" : "SURPRISE");
    Serial.println("            identity only - it reaches no accumulator, form");
    Serial.println("            choice, care rate or discipline roll.");

    Serial.printf("  age     : %.2f days = %u Visitor years  (stage %s)\n",
                  (double)pet_age_days(), p->days_alive, pet_stage_name(p->stage));

    Serial.println("  HOW I GREW UP (the persisted path, one rung per stage):");
    static const char *SL[4] = { "Baby", "Kid", "Teen", "Adult" };
    const uint8_t reached = (p->stage >= STAGE_BABY)
                          ? (uint8_t)(p->stage - STAGE_BABY + 1) : 0;
    if (!reached) Serial.println("    (still an egg)");
    for (uint8_t i = 0; i < reached && i < 4; i++)
        Serial.printf("    %-6s %s\n", SL[i],
                      (i == 0 || p->evo_path[i])
                        ? forms_long_name(i == 0 ? FORM_BABY : p->evo_path[i])
                        : "(not recorded - save predates 9.5)");
    for (uint8_t i = reached; i < 4; i++)
        Serial.printf("    %-6s (not reached yet)\n", SL[i]);

    /* THE SLEEP PERIOD. Printed because the dream rules are decided from it
     * and from nothing else - if a night ever produces two dreams, or none,
     * these three values are where the answer is. */
    Serial.printf("  sleep   : %s%s%s  recorded %lu min\n",
                  (p->sleep_flags & SLEEPF_IN_PERIOD) ? "period OPEN" : "awake",
                  (p->sleep_flags & SLEEPF_NAP)       ? " (nap)"      : "",
                  (p->sleep_flags & SLEEPF_DREAMT)    ? " ALREADY DREAMT" : "",
                  (unsigned long)(p->sleep_accum_sec / 60));
    Serial.printf("            needs %lu min (night) / %lu min (nap) to dream\n",
                  (unsigned long)(DREAM_MIN_NIGHT_SEC / 60),
                  (unsigned long)(DREAM_MIN_NAP_SEC / 60));
    if (p->pending_dream)
        Serial.printf("            a dream is WAITING to be told: \"%s\"\n",
                      dialogue_dream_bubble((uint8_t)(p->pending_dream - 1)));

    Serial.printf("  dreams  : %u kept of %u in the table\n",
                  p->dream_n, dialogue_dream_count());
    for (uint8_t i = 0; i < p->dream_n; i++)
        Serial.printf("    %u: %s\n", p->dream_id[i],
                      dialogue_dream_journal(p->dream_id[i]));

    Serial.printf("  learned behaviour %.1f  (0 calm .. 100 wild; %u opportunities, "
                  "%u ignored)\n", p->learned_mischief, p->disc_opportunities,
                  p->disc_ignored);
    Serial.printf("  deferred bubbles queued: %u\n", ui_bubble_deferred_count());
    Serial.println("-----------------------------------------------------------");
}

void diag_help(void)
{
    Serial.println();
    Serial.println("COMMANDS  (type a letter + Enter, or just the letter)");
    Serial.println("  x  GUIDED IMU axis capture (measurement 4) <- do this one");
    Serial.println("  i  toggle the raw 10 Hz IMU axis stream");
    Serial.println("  r  re-read the RTC          (measurement 7)");
    Serial.println("  o  test clearing the RTC OS flag (measurement 7b)");
    Serial.println("  f  display throughput / fps (measurement 2)");
    Serial.println("  m  memory + LVGL heap       (measurement 3)");
    Serial.println("  b  brightness sweep         (measurement 6)");
    Serial.println("  d  I2C scan                 (measurement 1)");
    Serial.println("  s  storage self-test");
    Serial.println("  W  WIPE saved state (capital W; asks for confirmation)");
    Serial.println("  ?  this help");
    Serial.println("  --- Phase 2: pet, animation, bubbles ---");
    Serial.println("  1  idle      2  blink     3  walk");
    Serial.println("  4  react     5  happy     6  sad");
    Serial.println("  e  cycle eye style        w  cycle mouth style");
    Serial.println("  k  cycle pet weight (min / start / max)");
    Serial.println("  --- Phase 3B: food, care, bathroom, messes ---");
    Serial.println("  7  burger    8  fruit     9  cake");
    Serial.println("  0  Bathroom action        C  Clean action");
    Serial.println("  T  fast-forward 30 simulated minutes");
    Serial.println("  R  care state report");
    Serial.println("  X  RESET the Visitor (newborn stats, clean room)");
    Serial.println("  --- Phase 5+6: clock, sleep, offline catch-up ---");
    Serial.println("  c  set the RTC to a demo time (clears the OS flag)");
    Serial.println("  l  toggle Lights (affects sleep recovery)");
    Serial.println("  h  simulate 8 h away    H  simulate 72 h    j  simulate 8 days");
    Serial.println("  p  force a save now + write/skip counters");
    Serial.println("  V  v1 -> v8   #  v6 -> v8   {  v5 -> v8   \"  v7 -> v8");
    Serial.println("  Y  persistence fidelity test (freezes sim, then reboot)");
    Serial.println("  y  toggle simulation suspend");
    Serial.println("  N  clock -> 20:30 bedtime   G  -> 07:30 wake   A  -> 13:30 nap");
    Serial.println("  u  open the Set Date & Time screen");
    Serial.println("  --- Phase 7: games ---");
    Serial.println("  Q  Higher/Lower  q  Reaction  E  Memory  z  Tilt Maze");
    Serial.println("  a  game records report   K  force-exit a game");
    Serial.println("  <  explain evolution scores   >  discipline report");
    Serial.println("  /  force a mischief window (test)");
    Serial.println("  F  force next stage + evolution   O  arm offline evo reveal");
    Serial.println("  +  EXCELLENT care   =  MID care   &  GOOD care   _  POOR care");
    Serial.println("  ,  age +1h   .  age +6h   %  age +24h   *  age clock report");
    Serial.println("  $  departure report + calibration   !  age pending departure 24h");
    Serial.println("  J  visit records report   @  jump to departure   ;  acknowledge");
    Serial.println("  :  START the egg (90s)   |  cycle colour   -  cycle gender");
    Serial.println("  --- Phase 9.5: personality, dreams, identity ---");
    Serial.println("  U  dialogue samples + About Me   I  identity / growth / behaviour");
    Serial.println("  ~  FORCE a dream (bypasses the rules)   ^  dream ELIGIBILITY rules");
    Serial.println("  (  deferred-reaction test        )  old-mess comment samples");
    Serial.println("  }  learned-behaviour recovery demo");
    Serial.println("  B  one sample bubble (cycles tiers)");
    Serial.println("  S  BUBBLE STRESS TEST - 20 requests, most should refuse");
    Serial.println("  L  BUBBLE LAYOUT TEST - 4 strings x 3 pet positions");
    Serial.println("  n  clear face overrides (back to the form's own face)");
    Serial.println("  P  pet screen        D  Phase 1 test card");
    Serial.println("  --- Phase 3A: menu, pages, gestures ---");
    Serial.println("  M  toggle menu       [ / ]  previous / next page");
    Serial.println("  g  toggle gesture measurement");
    Serial.println("  t  cycle transition SLIDE/FADE/CUT");
    Serial.println("  v  pager + pet state report");
    Serial.println("  Z  page sweep (2 full loops, checks wrap + leaks)");
    Serial.println(LINE);
}

void diag_serial_tick(void)
{
    static bool wipe_armed = false;
    while (Serial.available()) {
        const int c = Serial.read();

        /* During guided capture, ANY key means "I am holding the pose".
         * Consume newlines here too, since most terminals send them. */
        if (s_cap_step > 0) {
            if (c == '\r') continue;      /* avoid double-firing on CRLF */
            cap_take();
            continue;
        }
        if (c == '\r' || c == '\n') continue;

        if (wipe_armed) {
            wipe_armed = false;
            if (c == 'y' || c == 'Y') {
                Serial.printf("storage wipe: %s\n", storage_wipe() ? "done" : "FAILED");
            } else {
                Serial.println("storage wipe: cancelled");
            }
            continue;
        }

        switch (c) {
            case 'x': diag_imu_capture_start(); break;
            case 'i':
                s_imu_stream = !s_imu_stream;
                Serial.printf("IMU stream %s\n", s_imu_stream ? "ON" : "OFF");
                break;
            case 'r': diag_rtc_report();        break;
            case 'o': diag_rtc_clear_os();      break;
            case 'f': diag_flush_report();      break;
            case 'm': diag_lvgl_heap_report();  break;
            case 'b': diag_brightness_sweep();  break;
            case 'd': diag_i2c_report();        break;
            case 's': diag_storage_report();    break;
            case 'W':
                wipe_armed = true;
                Serial.println("WIPE saved state? press y to confirm, anything else cancels");
                break;
            case '1': diag_pet_anim(PET_ANIM_IDLE);  break;
            case '2': ui_pet_force_blink();
                      Serial.println("PET blink (forced)"); break;
            case '3': diag_pet_anim(PET_ANIM_WALK);  break;
            case '4': diag_pet_anim(PET_ANIM_REACT); break;
            case '5': diag_pet_anim(PET_ANIM_HAPPY); break;
            case '6': diag_pet_anim(PET_ANIM_SAD);   break;
            case 'e': diag_pet_eyes();           break;
            case 'w': diag_pet_mouth();          break;
            case 'k': diag_pet_live();           break;
            case 'n': ui_pet_set_face(-1, -1, -1);
                      Serial.println("PET face -> form default"); break;
            case 'B': diag_bubble_one();         break;
            case 'S': diag_bubble_spam();        break;
            case 'L': diag_bubble_layout();      break;
            case '7': care_feed(FOOD_BURGER);    break;
            case '8': care_feed(FOOD_FRUIT);     break;
            case '9': care_feed(FOOD_CAKE);      break;
            case '0': care_bathroom();           break;
            case 'C': care_clean();              break;
            case 'T': care_fast_forward(30);     break;
            case 'R': care_report();             break;
            case 'c': diag_rtc_set_demo();       break;
            case 'h': diag_simulate_absence(8);   break;
            case 'H': diag_simulate_absence(72);  break;
            case 'j': diag_simulate_absence(192); break;   /* 8 days */
            /* The console stands in for the player here, so it goes through
             * the PLAYER entry point - otherwise 'l' would silently exercise
             * a different code path from the Care page button and the lights
             * reaction could never be tested from the console at all. */
            case 'l': care_player_toggle_lights(!care_lights_on());
                      Serial.printf("lights %s\n", care_lights_on() ? "ON" : "off");
                      break;
            case 'p': persist_save(true); persist_report(); break;
            case 'N': diag_clock_to(20, 30, "night / bedtime");  break;
            case 'G': diag_clock_to(7, 30, "morning / wake");     break;
            case 'A': diag_clock_to(13, 30, "afternoon nap");     break;
            case 'u': setclock_open();
                      Serial.println("Set Date & Time opened (step 1 of 2)");
                      break;
            case 'Q': games_launch(GAME_HILO);   break;
            case 'q': games_launch(GAME_REACT);  break;
            case 'E': games_launch(GAME_MEMORY); break;
            case 'z': games_launch(GAME_MAZE);   break;
            case 'a': gamerec_report();          break;
            case 'W'+256: break;
            case 'w'+256: break;
            case 'x'+256: break;
            case '<': evolve_explain();          break;
            case '>': discipline_report();       break;
            case '1'+256: break;
            case 'F': {   /* force the next stage + its evolution, live */
                pet_state_t *pp = pet_mutable();
                const uint8_t next = (pp->stage < STAGE_ADULT) ? pp->stage + 1 : STAGE_ADULT;
                if (!pp->hatch_ts) pp->hatch_ts = 1;
                pp->stage = next;
                pp->days_alive = (next == STAGE_KID) ? STAGE_DAY_KID
                               : (next == STAGE_TEEN) ? STAGE_DAY_TEEN
                               : (next == STAGE_ADULT) ? STAGE_DAY_ADULT : 0;
                const uint8_t f = evolve_pick_form(next);
                Serial.printf("(test) forcing %s -> %s\n",
                              pet_stage_name(next), forms_name(f));
                evolve_present(f, false);
                /* Go through the SAME stage-entry path the real transition
                 * uses, so the growth spurt and per-stage counter resets are
                 * actually exercised by the test hook. */
                evolve_on_stage_entered(next, pp->days_alive);
                break;
            }
            case '?'+512: break;
            case '.': diag_time_travel(6);       break;
            case ',': diag_time_travel(1);       break;
            case '*': diag_age_report();         break;
            case 'J': visitrec_report();         break;
            case ':': {   /* start the egg timer, shortened for testing */
                if (pet_get()->stage != STAGE_EGG) { Serial.println("not an egg"); break; }
                /* THE SAME function the START button calls. This used to set
                 * egg_hatch_ts directly, which skipped the Surprise
                 * resolution entirely - so the command everyone would reach
                 * for to test hatching was driving a path the product does
                 * not have. */
                Serial.println("(test) pressing START on the player's behalf, 90 s timer");
                /* 90 s, not 10: long enough to power-cycle the device mid-hatch
                 * and prove the resolved identity came back from NVS rather
                 * than being rerolled. */
                scr_main_egg_start(90);
                break;
            }
            case '|': {   /* cycle the COLOUR choice, as the swatches do */
                pet_state_t *pp = pet_mutable();
                if (pp->stage != STAGE_EGG || pp->egg_hatch_ts) {
                    Serial.println("colour is locked - not a fresh egg"); break;
                }
                pp->egg_choice = (uint8_t)((pp->egg_choice + 1) % (EGG_PALETTE_COUNT + 1));
                if (pp->egg_choice < EGG_PALETTE_COUNT) pp->egg_color = pp->egg_choice;
                Serial.printf("EGG: colour choice -> %s\n",
                              pp->egg_choice >= EGG_PALETTE_COUNT
                                ? "SURPRISE (rainbow shell, resolved at START)"
                                : "explicit");
                persist_mark_dirty("egg colour");
                break;
            }
            case '-': {   /* cycle the GENDER choice, as the buttons do */
                pet_state_t *pp = pet_mutable();
                if (pp->stage != STAGE_EGG || pp->egg_hatch_ts) {
                    Serial.println("gender is locked - not a fresh egg"); break;
                }
                pp->gender_choice = (uint8_t)((pp->gender_choice + 1) % 3);
                Serial.printf("EGG: gender choice -> %s\n",
                              pp->gender_choice == GENDER_BOY  ? "Boy"  :
                              pp->gender_choice == GENDER_GIRL ? "Girl" : "SURPRISE");
                persist_mark_dirty("gender choice");
                break;
            }
            case ';': farewell_acknowledge();    break;
            case 'B'+512: break;
            case '@': {
                /* Jump to the PROJECTED departure by moving hatch_ts, so the
                 * age clock genuinely reads past the date and the witnessing
                 * rules in farewell_due() actually run. The old version set
                 * days_alive directly and then called farewell_begin() by
                 * hand, which tested the SCREEN and nothing else. */
                pet_state_t *pp = pet_mutable();
                if (!pp->hatch_ts || !rtc_trusted()) {
                    Serial.println("no trusted clock / not hatched - cannot jump");
                    break;
                }
                if (pp->depart_day <= 0.0f) {
                    Serial.println("no projection yet - forcing one");
                    visit_advance(VISIT_DEPART_EVAL_HOURS);
                }
                const float want = pp->depart_day > 0.0f ? pp->depart_day
                                                         : VISIT_DEPART_MIN_DAY;
                pp->hatch_ts = rtc_now() - (uint32_t)(want * 86400.0f) - 60UL;
                pet_refresh_age();
                pet_apply_stage_for_day(pet_age_days());
                Serial.printf("(test) jumped to day %.2f - departure is due; the "
                              "farewell now waits to be WITNESSED\n",
                              (double)pet_age_days());
                visit_report();
                break;
            }
            case '$': visit_report();            break;
            case '!': {
                /* Back-date a PENDING departure by 24 h, so the 48 h hold cap
                 * can be crossed without waiting two real days. Moves the
                 * stamp, not the clock: shifting the RTC would also move the
                 * sleep window, which is the very thing being tested. */
                pet_state_t *pp = pet_mutable();
                if (!pp->depart_due_ts) {
                    Serial.println("departure is not pending - nothing to age");
                    break;
                }
                pp->depart_due_ts -= 24UL * 3600UL;
                const uint32_t now = rtc_now();
                Serial.printf("(test) pending departure is now %lu h old "
                              "(cap %d) - asleep: %s\n",
                              (unsigned long)((now - pp->depart_due_ts) / 3600UL),
                              VISIT_HOLD_MAX_HOURS,
                              pet_get()->asleep ? "YES, must still wait" : "no");
                persist_mark_dirty("aged pending departure");
                persist_save(true);
                break;
            }
            case '&': diag_seed_care(3);         break;
            case '%': diag_time_travel(24);      break;
            case '+': diag_seed_care(2);         break;
            case '=': diag_seed_care(1);         break;
            case '_': diag_seed_care(0);         break;
            case 'O': {   /* fake an offline evolution announcement */
                pet_mutable()->evo_announce = 1;
                Serial.println("(test) evo_announce set - reboot to see the reveal");
                persist_save(true);
                break;
            }
            case 'G'+256: break;
            case '/': discipline_misbehave(MIS_MESS);
                      Serial.println("(test) forced a mischief window"); break;
            case 'Q'+128: break;
            case 'K': games_force_exit();        break;
            case 'Y': diag_persist_fidelity();   break;
            case 'y': pet_set_sim_suspended(!pet_sim_suspended());
                      Serial.printf("simulation %s\n",
                                    pet_sim_suspended() ? "SUSPENDED" : "running");
                      break;
            case 'v'+1024: break;
            case '{': {   /* v5 -> v7: the older chain, still exercised */
                Serial.println();
                Serial.println("=== v5 -> v8 MIGRATION TEST (older chain) =================");
                const uint32_t hatch = (rtc_trusted() ? rtc_now() : 1787000000UL)
                                     - (uint32_t)(11.4f * 86400.0f);
                storage_write_fake_v5(hatch, 44.0f, 81.0f, FORM_ADULT_SWEET,
                                      PERS_TIDY, PERS_CURIOUS);
                const load_result_t r = persist_load();
                const pet_state_t *q = pet_get();
                Serial.printf("  result       : %s  (expect MIGRATED)\n",
                              storage_load_result_str(r));
                Serial.printf("  hunger       : %.0f   (expect 44 - preserved)\n", q->hunger);
                Serial.printf("  care_happy   : %.0f   (expect 81 - accumulator kept)\n",
                              q->care_happy);
                Serial.printf("  personality  : %s + %s  (expect tidy + curious)\n",
                              evolve_trait_name(q->trait_a), evolve_trait_name(q->trait_b));
                Serial.printf("  form         : %s   (expect Sweet - NOT re-picked)\n",
                              forms_name(q->form_id));
                Serial.printf("  evo_path     : %u/%u/%u/%u  (preserved)\n",
                              q->evo_path[0], q->evo_path[1], q->evo_path[2],
                              q->evo_path[3]);
                Serial.printf("  evo_announce : %u   (MUST be 0 - no replayed reveal)\n",
                              q->evo_announce);
                Serial.printf("  journal      : %u entries  (preserved)\n",
                              journal_count());
                Serial.printf("  bath_target  : %.2f h  (preserved)\n", q->bath_target_h);
                Serial.printf("  age          : %.2f days -> stage %s (recomputed)\n",
                              (double)pet_age_days(), pet_stage_name(q->stage));
                Serial.printf("  depart_day   : %.2f  (expect 0 - new in v6)\n",
                              (double)q->depart_day);
                Serial.printf("  depart_lock  : %u   (expect 0 - new in v6)\n",
                              q->depart_locked);
                Serial.println("  now let it project: the notice floor must keep a save");
                Serial.println("  that is ALREADY past day 9 from departing in the past.");
                visit_advance(VISIT_DEPART_EVAL_HOURS);
                visit_report();
                break;
            }
            case '"': {   /* v7 -> v8: the hop the sleep period added */
                Serial.println();
                Serial.println("=== v7 -> v8 MIGRATION TEST ===============================");
                const uint32_t hatch = (rtc_trusted() ? rtc_now() : 1787000000UL)
                                     - (uint32_t)(11.4f * 86400.0f);
                storage_write_fake_v7(hatch, 44.0f, 81.0f, FORM_ADULT_SWEET,
                                      PERS_TIDY, PERS_CURIOUS);
                const load_result_t r = persist_load();
                const pet_state_t *q = pet_get();
                Serial.printf("  result       : %s  (expect MIGRATED)\n",
                              storage_load_result_str(r));
                Serial.printf("  hunger       : %.0f   (expect 44)\n", q->hunger);
                Serial.printf("  evo_path     : %s -> %s -> %s -> %s\n",
                              forms_long_name(q->evo_path[0]), forms_long_name(q->evo_path[1]),
                              forms_long_name(q->evo_path[2]), forms_long_name(q->evo_path[3]));
                Serial.printf("  depart_day   : %.2f lock %u  (pacing state kept)\n",
                              (double)q->depart_day, q->depart_locked);
                Serial.println("  --- the schema-7 tail must be UNTOUCHED ---");
                Serial.printf("  gender       : %s, choice %s  (expect girl / SURPRISE)\n",
                              q->gender == GENDER_GIRL ? "girl" : "boy",
                              q->gender_choice == GENDER_SURPRISE ? "SURPRISE" : "explicit");
                Serial.printf("  learned      : %.1f   (expect 73.5 - NOT reset)\n",
                              q->learned_mischief);
                Serial.printf("  dreams       : %u kept  (expect 3)\n", q->dream_n);
                Serial.println("  --- the new schema-8 tail ---");
                Serial.printf("  sleep period : %s  recorded %lu min  (expect closed / 0 -\n",
                              (q->sleep_flags & SLEEPF_IN_PERIOD) ? "OPEN" : "closed",
                              (unsigned long)(q->sleep_accum_sec / 60));
                Serial.println("                 a zeroed tail IS correct here: no period was open)");
                Serial.printf("  pending dream: %u   (expect 0)\n", q->pending_dream);
                Serial.println("-----------------------------------------------------------");
                break;
            }
            case '#': {   /* v6 -> v8 */
                Serial.println();
                Serial.println("=== v6 -> v8 MIGRATION TEST ===============================");
                const uint32_t hatch = (rtc_trusted() ? rtc_now() : 1787000000UL)
                                     - (uint32_t)(11.4f * 86400.0f);
                storage_write_fake_v6(hatch, 44.0f, 81.0f, FORM_ADULT_SWEET,
                                      PERS_TIDY, PERS_CURIOUS);
                const load_result_t r = persist_load();
                const pet_state_t *q = pet_get();
                Serial.printf("  result       : %s  (expect MIGRATED)\n",
                              storage_load_result_str(r));
                Serial.printf("  hunger       : %.0f   (expect 44 - preserved)\n", q->hunger);
                Serial.printf("  care_happy   : %.0f   (expect 81 - accumulator kept)\n",
                              q->care_happy);
                Serial.printf("  personality  : %s + %s  (expect tidy + curious)\n",
                              evolve_trait_name(q->trait_a), evolve_trait_name(q->trait_b));
                Serial.printf("  form         : %s   (expect Sweet - NOT re-picked)\n",
                              forms_name(q->form_id));
                Serial.printf("  evo_path     : %s -> %s -> %s -> %s  (preserved)\n",
                              forms_long_name(q->evo_path[0]), forms_long_name(q->evo_path[1]),
                              forms_long_name(q->evo_path[2]), forms_long_name(q->evo_path[3]));
                Serial.printf("  evo_announce : %u   (MUST be 0 - no replayed reveal)\n",
                              q->evo_announce);
                Serial.printf("  journal      : %u entries  (preserved)\n", journal_count());
                Serial.printf("  depart_day   : %.2f  (expect 12.50 - PACING STATE KEPT)\n",
                              (double)q->depart_day);
                Serial.printf("  depart_lock  : %u   (expect 1 - the lock survives)\n",
                              q->depart_locked);
                Serial.println("  --- the new schema-7 tail ---");
                Serial.printf("  gender       : %s, choice %s\n",
                              q->gender == GENDER_GIRL ? "girl" : "boy",
                              q->gender_choice == GENDER_SURPRISE ? "SURPRISE (correct: the "
                              "player was never asked)" : "explicit (WRONG for a v6 save)");
                Serial.printf("  learned      : %.1f   (expect %.0f - NEUTRAL, not 0)\n",
                              q->learned_mischief, (double)LEARN_START);
                Serial.printf("  dreams       : %u   (expect 0 - none have happened)\n",
                              q->dream_n);
                Serial.println("-----------------------------------------------------------");
                break;
            }
            case 'U': dialogue_report();         break;
            case 'I': diag_identity_report();    break;
            case '~': {
                Serial.println("(test) forcing a NIGHT dream");
                care_dream(false, false);
                break;
            }
            case '^':
                /* The RULES, run against constructed periods - durations,
                 * one-per-period, and the nap roll as a distribution. */
                ui_bubble_set_suppressed(true);
                care_dream_rules_probe();
                ui_bubble_set_suppressed(false);
                break;
            case '(': {
                /* The deferred-reaction proof. Opens the menu, fires a
                 * reaction into it, and shows the queue holding it - the
                 * whole failure this mechanism exists to fix. */
                Serial.println();
                Serial.println("=== DEFERRED REACTION TEST ================================");
                if (!menu_is_open()) menu_open();
                Serial.printf("  menu open: %s   bubbles suppressed: %s\n",
                              menu_is_open() ? "yes" : "NO",
                              ui_bubble_suppressed() ? "yes" : "NO");
                ui_bubble_say_deferred(BUBBLE_T1_REACTION, dialogue_lights_off());
                Serial.printf("  queued: %u  (a plain ui_bubble_say() would be LOST here)\n",
                              ui_bubble_deferred_count());
                Serial.println("  closing the menu - it should appear on the pet screen now");
                menu_close();
                Serial.println("-----------------------------------------------------------");
                break;
            }
            case ')': {
                /* The REAL trigger, not just the line table. */
                care_stink_probe();
                Serial.println("  line variants for this Visitor:");
                for (uint8_t i = 0; i < 6; i++)
                    Serial.printf("    %s\n", dialogue_stink());
                break;
            }
            case '}': {
                /* RECOVERABILITY, demonstrated rather than asserted. Five
                 * ignored windows then five corrected ones: the record must
                 * climb, then come back down. Nothing is ever locked. */
                Serial.println();
                Serial.println("=== LEARNED BEHAVIOUR: IS IT RECOVERABLE? =================");
                pet_state_t *q = pet_mutable();
                const float was = q->learned_mischief;
                Serial.printf("  start          %.1f\n", was);
                for (uint8_t i = 0; i < 5; i++) {
                    q->learned_mischief += LEARN_ALPHA * (100.0f - q->learned_mischief);
                    Serial.printf("  ignored  #%u -> %.1f\n", i + 1, q->learned_mischief);
                }
                for (uint8_t i = 0; i < 5; i++) {
                    q->learned_mischief += LEARN_ALPHA * (0.0f - q->learned_mischief);
                    Serial.printf("  corrected #%u -> %.1f\n", i + 1, q->learned_mischief);
                }
                Serial.printf("  finished at %.1f: neglect RAISES it, later care LOWERS it,\n",
                              q->learned_mischief);
                Serial.println("  and neither direction is ever locked in.");
                q->learned_mischief = was;      /* a test must not alter history */
                Serial.printf("  restored to %.1f (the demo does not change the Visitor)\n", was);
                Serial.println("-----------------------------------------------------------");
                break;
            }
            case 'V': {
                Serial.println();
                Serial.println("=== v1 -> v8 MIGRATION TEST (oldest chain) ================");
                storage_write_fake_v1(41.0f, 58.5f, 4);
                const load_result_t r = persist_load();
                const pet_state_t *q = pet_get();
                Serial.printf("  result   : %s\n", storage_load_result_str(r));
                Serial.printf("  hunger   : %.0f  (expected 41)\n", q->hunger);
                Serial.printf("  weight   : %.1f  (expected 58.5)\n", q->weight_g);
                Serial.printf("  days     : %u   (expected 4)\n", q->days_alive);
                Serial.printf("  bathroom : %.0f  (expected 0 - new in v2)\n", q->bathroom);
                Serial.printf("  messes   : %u   (expected 0 - new in v2)\n", care_mess_count());
                Serial.println("-----------------------------------------------------------");
                break;
            }
            case 'X':
                pet_init();
                care_reset();
                Serial.println("=== VISITOR RESET to a newborn Baby ===");
                care_report();
                break;
            case 'M': menu_toggle();             break;
            case '[': menu_step(-1);             break;
            case ']': menu_step(1);              break;
            case 'g': menu_set_metrics(!menu_metrics());
                      Serial.printf("gesture metrics %s\n", menu_metrics() ? "ON" : "off");
                      break;
            case 't': menu_set_transition((menu_transition() + 1) % 3);
                      Serial.printf("transition -> %s\n", menu_transition_name());
                      break;
            case 'v': diag_menu_report();        break;
            case 'Z': diag_page_sweep();         break;
            case 'P': diag_screen(true);         break;
            case 'D': diag_screen(false);        break;
            case '?': diag_help();              break;
            default:  Serial.printf("? unknown '%c' - press ? for help\n", (char)c); break;
        }
    }
}
