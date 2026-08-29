/* rtc - PCF85063A. See rtc.h for why the OS flag is handled so carefully. */

#include <Arduino.h>
#include <string.h>

#include "board_pins.h"
#include "config.h"
#include "bsp.h"
#include "rtc.h"

#define PCF_REG_CTRL1    0x00
#define PCF_REG_SECONDS  0x04       /* bit 7 = OS (oscillator stop) */

static rtc_health_t s_health = RTC_ABSENT;

static uint8_t bcd2dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static uint8_t dec2bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

/* Days since 1970-01-01. Civil-from-days, valid well past this product's
 * lifetime and with no dependence on the C library's timezone handling. */
static uint32_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (int)doe - 719468);
}

static uint32_t to_unix(const rtc_time_t *t)
{
    return days_from_civil(t->year, t->month, t->day) * 86400UL
         + t->hour * 3600UL + t->min * 60UL + t->sec;
}

bool rtc_read(rtc_time_t *out)
{
    uint8_t d[7];
    if (!bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_SECONDS, d, sizeof(d))) return false;
    out->sec   = bcd2dec(d[0] & 0x7F);      /* bit 7 is OS, not a digit */
    out->min   = bcd2dec(d[1] & 0x7F);
    out->hour  = bcd2dec(d[2] & 0x3F);
    out->day   = bcd2dec(d[3] & 0x3F);
    out->month = bcd2dec(d[5] & 0x1F);
    out->year  = (uint16_t)(2000 + bcd2dec(d[6]));
    return true;
}

static bool os_flag_set(void)
{
    uint8_t sec;
    if (!bsp_i2c_read(I2C_ADDR_RTC, PCF_REG_SECONDS, &sec, 1)) return true;
    return (sec & 0x80) != 0;
}

static void evaluate_health(void)
{
    if (!bsp_i2c_probe(I2C_ADDR_RTC)) { s_health = RTC_ABSENT; return; }
    if (os_flag_set())                { s_health = RTC_UNSET;  return; }

    rtc_time_t t;
    if (!rtc_read(&t)) { s_health = RTC_ABSENT; return; }
    const uint32_t ts = to_unix(&t);
    /* A running clock that reads 2001 is not trustworthy just because the OS
     * flag happens to be clear. */
    s_health = (ts >= RTC_MIN_VALID_TS && ts <= RTC_MAX_VALID_TS)
             ? RTC_OK : RTC_IMPLAUSIBLE;
}

bool rtc_begin(void)
{
    evaluate_health();
    return s_health != RTC_ABSENT;
}

rtc_health_t rtc_health(void) { return s_health; }
bool rtc_trusted(void)        { return s_health == RTC_OK; }

uint32_t rtc_now(void)
{
    rtc_time_t t;
    if (s_health != RTC_OK || !rtc_read(&t)) return 0;
    return to_unix(&t);
}

bool rtc_set(const rtc_time_t *t)
{
    uint8_t d[7];
    d[0] = dec2bcd(t->sec) & 0x7F;      /* OS bit written as 0 == cleared */
    d[1] = dec2bcd(t->min);
    d[2] = dec2bcd(t->hour);
    d[3] = dec2bcd(t->day);
    d[4] = 0;                           /* weekday: unused by this project */
    d[5] = dec2bcd(t->month);
    d[6] = dec2bcd((uint8_t)(t->year % 100));

    for (uint8_t i = 0; i < 7; i++)
        if (!bsp_i2c_write8(I2C_ADDR_RTC, PCF_REG_SECONDS + i, d[i])) return false;

    /* Phase 1 verified a plain write clears OS with the oscillator running -
     * no CTRL1 STOP bracket needed. Verify rather than assume: if the clear
     * did not stick, the clock must stay untrusted. */
    delay(5);
    const bool still_set = os_flag_set();
    evaluate_health();

    if (still_set) {
        Serial.println("RTC set FAILED: OS flag did not clear - time stays untrusted");
        return false;
    }
    Serial.println("RTC set, OS flag cleared and verified");
    return s_health == RTC_OK;
}

void rtc_format(uint32_t ts, char *buf, size_t len)
{
    if (!ts) { snprintf(buf, len, "--:--"); return; }
    uint32_t days = ts / 86400UL, rem = ts % 86400UL;
    /* civil-from-days, the inverse of the above */
    uint32_t z = days + 719468;
    uint32_t era = z / 146097;
    uint32_t doe = z - era * 146097;
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t y = yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    snprintf(buf, len, "%04lu-%02lu-%02lu %02lu:%02lu:%02lu",
             (unsigned long)y, (unsigned long)m, (unsigned long)d,
             (unsigned long)(rem / 3600), (unsigned long)((rem % 3600) / 60),
             (unsigned long)(rem % 60));
}

const char *rtc_health_name(rtc_health_t h)
{
    switch (h) {
        case RTC_OK:          return "OK (trusted)";
        case RTC_UNSET:       return "UNSET (OS flag set - never been set)";
        case RTC_IMPLAUSIBLE: return "IMPLAUSIBLE (outside the valid window)";
        default:              return "ABSENT";
    }
}
