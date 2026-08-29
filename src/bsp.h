#pragma once
/* Board support: I2C bus, IO expander, display, touch, LVGL bring-up.
 * Knows nothing about the game. Nothing above this layer touches hardware. */

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool i2c_ok;
    bool ioexp_ok;        /* TCA9554 responded and the reset sequence ran */
    bool display_ok;      /* false when BSP_QSPI_VERIFIED == 0           */
    bool touch_ok;        /* CST820 responded and auto-sleep was disabled */
    bool lvgl_ok;
    uint8_t i2c_found[16];
    uint8_t i2c_count;
    uint32_t draw_buf_bytes;
} bsp_status_t;

/* Bring-up. Safe to call once, from setup(). Never aborts: a failed
 * subsystem is reported in the status struct, not fatal. */
void bsp_init(void);
const bsp_status_t *bsp_status(void);

/* --- raw I2C helpers (used by rtc/imu/power; no game logic here) -------- */
bool bsp_i2c_probe(uint8_t addr);
bool bsp_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
bool bsp_i2c_write8(uint8_t addr, uint8_t reg, uint8_t val);
void bsp_i2c_scan(uint8_t *out, uint8_t *count, uint8_t max);

/* --- display ------------------------------------------------------------ */
bool bsp_display_available(void);
void bsp_set_brightness(uint8_t level);   /* no-op when display unavailable */
void bsp_display_on(bool on);

/* --- flush instrumentation (Phase 1 measurement #2) ---------------------
 * Counters accumulated inside the real LVGL flush callback, so what we
 * report is the path the app actually uses, not a synthetic benchmark. */
typedef struct {
    uint64_t total_us;
    uint64_t total_px;
    uint32_t flushes;
    uint32_t max_us;      /* slowest single flush chunk */
} bsp_flush_stats_t;

void bsp_flush_stats_reset(void);
void bsp_flush_stats_get(bsp_flush_stats_t *out);

/* --- touch (raw, for the Phase 1 touch check) --------------------------- */
bool bsp_touch_read_raw(uint16_t *x, uint16_t *y, uint8_t *fingers);

#ifdef __cplusplus
}
#endif
