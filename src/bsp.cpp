/* Board support package - Waveshare ESP32-S3-Touch-AMOLED-1.8 V2 */

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "board_pins.h"
#include "config.h"
#include "bsp.h"

#if BSP_QSPI_VERIFIED
  #include <Arduino_GFX_Library.h>
  static Arduino_DataBus *s_bus = nullptr;
  static Arduino_CO5300 *s_gfx = nullptr;
#endif

static bsp_status_t s_st;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t        *s_buf1 = nullptr;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;

static volatile uint64_t s_flush_us  = 0;
static volatile uint64_t s_flush_px  = 0;
static volatile uint32_t s_flush_cnt = 0;
static volatile uint32_t s_flush_max = 0;

/* ==========================================================================
 * I2C
 * ======================================================================= */

bool bsp_i2c_probe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool bsp_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool bsp_i2c_write8(uint8_t addr, uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void bsp_i2c_scan(uint8_t *out, uint8_t *count, uint8_t max)
{
    uint8_t n = 0;
    /* 0x08..0x77 is the valid 7-bit addressable range; reserved addresses
     * outside it are not probed. 0x7E is inside the range and IS probed,
     * but read-only - we never write to it. */
    for (uint8_t a = 0x08; a <= 0x77 && n < max; a++) {
        if (bsp_i2c_probe(a)) out[n++] = a;
    }
    /* The brief lists an unidentified device at 0x7E, above the normal
     * scan range, so probe it explicitly. Probe only. Never write. */
    if (n < max && bsp_i2c_probe(I2C_ADDR_UNKNOWN)) out[n++] = I2C_ADDR_UNKNOWN;
    *count = n;
}

/* ==========================================================================
 * TCA9554 IO expander - the verified pre-display reset sequence
 * ======================================================================= */

static bool tca9554_reset_sequence(void)
{
    if (!bsp_i2c_probe(I2C_ADDR_IOEXP)) return false;

    uint8_t cfg = 0xFF, outv = 0xFF;
    bsp_i2c_read(I2C_ADDR_IOEXP, TCA9554_REG_CONFIG, &cfg,  1);
    bsp_i2c_read(I2C_ADDR_IOEXP, TCA9554_REG_OUTPUT, &outv, 1);

    /* Make bits 0,1,2,6 outputs; leave every other bit exactly as found.
     * We do not know what the other four bits drive, so we do not touch
     * them. CONFIG: 0 = output. */
    if (!bsp_i2c_write8(I2C_ADDR_IOEXP, TCA9554_REG_CONFIG,
                        (uint8_t)(cfg & ~BSP_TCA_RESET_MASK))) return false;

    /* LOW -> 20ms -> HIGH -> 50ms  (brief section 2, verified) */
    if (!bsp_i2c_write8(I2C_ADDR_IOEXP, TCA9554_REG_OUTPUT,
                        (uint8_t)(outv & ~BSP_TCA_RESET_MASK))) return false;
    delay(BSP_TCA_LOW_MS);
    if (!bsp_i2c_write8(I2C_ADDR_IOEXP, TCA9554_REG_OUTPUT,
                        (uint8_t)(outv | BSP_TCA_RESET_MASK))) return false;
    delay(BSP_TCA_HIGH_MS);

    return true;
}

/* ==========================================================================
 * CST820 touch
 *
 * Register map below is the CST816/CST820 family map. It is a property of
 * the touch part, not of this board, but it has NOT been confirmed on this
 * hardware - the Phase 1 touch test prints raw coordinates so corner taps
 * can be checked against the expected 0..367 / 0..447 ranges.
 * ======================================================================= */

#define CST820_REG_GESTURE   0x01
#define CST820_REG_FINGERNUM 0x02
#define CST820_REG_XH        0x03
#define CST820_REG_DISAUTO   0xFE   /* verified in brief: write 0x01 */

static bool cst820_init(void)
{
    if (!bsp_i2c_probe(I2C_ADDR_TOUCH)) return false;
    /* Disable auto-sleep. Verified requirement, brief section 2. */
    return bsp_i2c_write8(I2C_ADDR_TOUCH, CST820_REG_DISAUTO, 0x01);
}

bool bsp_touch_read_raw(uint16_t *x, uint16_t *y, uint8_t *fingers)
{
    uint8_t d[6];
    *x = *y = 0; *fingers = 0;
    if (!s_st.touch_ok) return false;
    /* Read gesture, finger count, XH, XL, YH, YL in one burst from 0x01 */
    if (!bsp_i2c_read(I2C_ADDR_TOUCH, CST820_REG_GESTURE, d, sizeof(d))) return false;

    *fingers = d[1] & 0x0F;
    if (*fingers == 0) return false;

    *x = (uint16_t)(((d[2] & 0x0F) << 8) | d[3]);
    *y = (uint16_t)(((d[4] & 0x0F) << 8) | d[5]);
    return true;
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint16_t x, y; uint8_t f;
    if (bsp_touch_read_raw(&x, &y, &f) && x < BSP_LCD_W && y < BSP_LCD_H) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

/* ==========================================================================
 * LVGL display driver
 * ======================================================================= */

/* CO5300 requires redraw regions on EVEN boundaries. Doing it in the
 * rounder callback covers every redraw - including animations - so no call
 * site can ever forget it. Screen is 368x448, so x2/y2 max out at 367/447,
 * both already odd; |=1 can never run off the panel. */
static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *a)
{
    (void)drv;
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
    if (a->x2 > BSP_LCD_W - 1) a->x2 = BSP_LCD_W - 1;
    if (a->y2 > BSP_LCD_H - 1) a->y2 = BSP_LCD_H - 1;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    const int64_t  t0 = esp_timer_get_time();

#if BSP_QSPI_VERIFIED
    /* The +16 column offset is applied inside Arduino_CO5300 via its
     * col_offset1 constructor argument, so LVGL coordinates go through
     * unmodified here. 16 is even, so it cannot disturb the parity the
     * rounder just established. */
    s_gfx->draw16bitRGBBitmap((int16_t)area->x1, (int16_t)area->y1,
                              (uint16_t *)px, (int16_t)w, (int16_t)h);
#endif

    const uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
    s_flush_us += dt;
    s_flush_px += (uint64_t)w * h;
    s_flush_cnt = s_flush_cnt + 1;   /* not ++: deprecated on volatile in C++20 */
    if (dt > s_flush_max) s_flush_max = dt;

    lv_disp_flush_ready(drv);
}

void bsp_flush_stats_reset(void)
{
    s_flush_us = 0; s_flush_px = 0; s_flush_cnt = 0; s_flush_max = 0;
}

void bsp_flush_stats_get(bsp_flush_stats_t *out)
{
    out->total_us = s_flush_us;
    out->total_px = s_flush_px;
    out->flushes  = s_flush_cnt;
    out->max_us   = s_flush_max;
}

/* ==========================================================================
 * Display control
 * ======================================================================= */

bool bsp_display_available(void) { return s_st.display_ok; }

void bsp_set_brightness(uint8_t level)
{
#if BSP_QSPI_VERIFIED
    if (s_gfx) s_gfx->setBrightness(level);
#else
    (void)level;
#endif
}

void bsp_display_on(bool on)
{
#if BSP_QSPI_VERIFIED
    if (!s_gfx) return;
    if (on) s_gfx->displayOn(); else s_gfx->displayOff();
#else
    (void)on;
#endif
}

/* ==========================================================================
 * Bring-up
 * ======================================================================= */

void bsp_init(void)
{
    memset(&s_st, 0, sizeof(s_st));

    /* --- 1. I2C ---------------------------------------------------------- */
    s_st.i2c_ok = Wire.begin(BSP_I2C_SDA, BSP_I2C_SCL, BSP_I2C_HZ);
    if (s_st.i2c_ok) {
        bsp_i2c_scan(s_st.i2c_found, &s_st.i2c_count, sizeof(s_st.i2c_found));
    }

    /* --- 2. TCA9554 reset sequence, BEFORE any display work -------------- */
    s_st.ioexp_ok = s_st.i2c_ok && tca9554_reset_sequence();

    /* --- 3. Display ------------------------------------------------------ */
#if BSP_QSPI_VERIFIED
    s_bus = new Arduino_ESP32QSPI(BSP_QSPI_CS, BSP_QSPI_SCK,
                                  BSP_QSPI_D0, BSP_QSPI_D1,
                                  BSP_QSPI_D2, BSP_QSPI_D3);
    s_gfx = new Arduino_CO5300(s_bus, BSP_LCD_RST, 0,
                               BSP_LCD_W, BSP_LCD_H,
                               BSP_LCD_COL_OFFSET, 0, 0, 0);
    s_st.display_ok = s_gfx->begin(BSP_QSPI_HZ);
    if (s_st.display_ok) {
        s_gfx->fillScreen(0x0000);
        s_gfx->setBrightness(BRIGHT_FULL);
    }
#else
    s_st.display_ok = false;
#endif

    /* --- 4. Touch -------------------------------------------------------- */
    s_st.touch_ok = s_st.i2c_ok && cst820_init();

    /* --- 5. LVGL ---------------------------------------------------------
     * LVGL is initialised even when the display is unavailable, with a
     * flush callback that only reports completion. That keeps the heap
     * measurement and all higher layers testable while the QSPI pins are
     * still unknown. */
    lv_init();

    s_st.draw_buf_bytes = BSP_LCD_W * LV_DRAW_BUF_LINES * sizeof(lv_color_t);
    s_buf1 = (lv_color_t *)heap_caps_malloc(s_st.draw_buf_bytes,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_buf1) { s_st.lvgl_ok = false; return; }

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr,
                          BSP_LCD_W * LV_DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res    = BSP_LCD_W;
    s_disp_drv.ver_res    = BSP_LCD_H;
    s_disp_drv.flush_cb   = flush_cb;
    s_disp_drv.rounder_cb = rounder_cb;
    s_disp_drv.draw_buf   = &s_draw_buf;
    s_disp_drv.full_refresh = 0;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    s_st.lvgl_ok = true;
}

const bsp_status_t *bsp_status(void) { return &s_st; }


/* --- battery, read-only. See bsp.h and config.h. ------------------------ */

static uint32_t s_bat_next_ms;
static uint32_t s_bat_ema_mv;      /* 0 = no sample yet */
static uint8_t  s_bat_pct_shown;
static bool     s_bat_valid;
static uint8_t  s_bat_st1_seen, s_bat_st2_seen;

/* mV -> percent, piecewise linear between the BATTERY_CURVE anchors. */
static uint8_t bat_curve_pct(uint16_t mv)
{
    static const struct { uint16_t mv; uint8_t pct; } C[] = BATTERY_CURVE;
    const size_t n = sizeof(C) / sizeof(C[0]);

    if (mv >= C[0].mv)     return C[0].pct;
    if (mv <= C[n-1].mv)   return C[n-1].pct;
    for (size_t i = 1; i < n; i++) {
        if (mv >= C[i].mv) {
            /* Between C[i] (lower) and C[i-1] (higher). */
            const int32_t span_mv  = (int32_t)C[i-1].mv  - (int32_t)C[i].mv;
            const int32_t span_pct = (int32_t)C[i-1].pct - (int32_t)C[i].pct;
            if (span_mv <= 0) return C[i].pct;
            const int32_t up = (int32_t)mv - (int32_t)C[i].mv;
            return (uint8_t)(C[i].pct + (up * span_pct + span_mv / 2) / span_mv);
        }
    }
    return 0;
}

void bsp_battery_tick(void)
{
    const uint32_t now = millis();
    if (s_bat_next_ms && (int32_t)(now - s_bat_next_ms) < 0) return;
    s_bat_next_ms = now + BATTERY_POLL_MS;

    uint8_t d[2];
    if (!bsp_i2c_read(I2C_ADDR_PMIC, 0x34, d, 2)) return;
    const uint16_t raw = (uint16_t)(((d[0] & 0x3F) << 8) | d[1]);

    /* Observe the status bytes without acting on them - see bsp.h. */
    uint8_t st;
    if (bsp_i2c_read(I2C_ADDR_PMIC, 0x00, &st, 1)) s_bat_st1_seen |= st;
    if (bsp_i2c_read(I2C_ADDR_PMIC, 0x01, &st, 1)) s_bat_st2_seen |= st;

    /* A reading outside the plausible window is not a battery. Reject it and
     * keep the last good value rather than showing a wrong one. */
    if (raw < BATTERY_MV_MIN || raw > BATTERY_MV_MAX) {
        Serial.printf("BATTERY: implausible reading %u mV - ignored\n", raw);
        return;
    }

    s_bat_ema_mv = s_bat_ema_mv
        ? (uint32_t)((s_bat_ema_mv * (BATTERY_EMA_DEN - BATTERY_EMA_NUM)
                      + (uint32_t)raw * BATTERY_EMA_NUM) / BATTERY_EMA_DEN)
        : raw;

    const uint8_t pct = bat_curve_pct((uint16_t)s_bat_ema_mv);
    if (!s_bat_valid) {
        s_bat_pct_shown = pct;
        s_bat_valid = true;
        Serial.printf("BATTERY: first reading %u mV -> %u%%\n", raw, pct);
    } else {
        const int diff = (int)pct - (int)s_bat_pct_shown;
        if (diff >= BATTERY_HYST_PCT || diff <= -BATTERY_HYST_PCT) {
            Serial.printf("BATTERY: %u mV (ema %lu) -> %u%%\n", raw,
                          (unsigned long)s_bat_ema_mv, pct);
            s_bat_pct_shown = pct;
        }
    }
}

bool     bsp_battery_valid(void) { return s_bat_valid; }
uint16_t bsp_battery_mv(void)    { return (uint16_t)s_bat_ema_mv; }
uint8_t  bsp_battery_pct(void)   { return s_bat_pct_shown; }

void bsp_battery_status_seen(uint8_t *st1_mask, uint8_t *st2_mask)
{
    if (st1_mask) *st1_mask = s_bat_st1_seen;
    if (st2_mask) *st2_mask = s_bat_st2_seen;
}
