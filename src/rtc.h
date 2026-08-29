#pragma once
/* ===========================================================================
 * rtc - PCF85063A. The authoritative wall clock.  [MILESTONE 5]
 *
 * THE OS FLAG IS THE WHOLE POINT OF THIS MODULE'S CARE.
 * Phase 1 established on hardware that the oscillator-stop flag is a STICKY
 * "clock integrity not guaranteed" bit: hardware sets it when the oscillator
 * stops, and ONLY software clears it. So rtc_set() must clear it explicitly
 * and confirm the clear, or the flag stays set forever and carries no
 * information at all - and every later "is this elapsed time real?" decision
 * is then built on a lie.
 *
 * Phase 1 also proved the board keeps time across complete power removal, so
 * offline continuation is achievable on this hardware.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;  uint8_t month, day;
    uint8_t  hour, min, sec;
} rtc_time_t;

typedef enum {
    RTC_OK = 0,        /* present, running, OS clear -> time is trusted    */
    RTC_UNSET,         /* present but OS set -> never been set, do not trust */
    RTC_IMPLAUSIBLE,   /* reads outside the plausibility window            */
    RTC_ABSENT         /* no response at 0x51                              */
} rtc_health_t;

/* NOTE: named rtc_begin(), not rtc_init(). ESP-IDF's libesp_hw_support
 * already exports a global rtc_init() for the SoC's own RTC peripheral, and
 * a C-linkage clash there is a link error, not a compile error. */
bool         rtc_begin(void);
rtc_health_t rtc_health(void);
bool         rtc_trusted(void);          /* health == RTC_OK               */

bool     rtc_read(rtc_time_t *out);
uint32_t rtc_now(void);                  /* unix-ish seconds; 0 if untrusted */

/* Writes the time AND clears the OS flag, then verifies it read back clear.
 * Returns false if the clear did not stick - which must be treated as "the
 * clock is still untrusted", not as a cosmetic failure. */
bool rtc_set(const rtc_time_t *t);

void rtc_format(uint32_t ts, char *buf, size_t len);
const char *rtc_health_name(rtc_health_t h);

#ifdef __cplusplus
}
#endif
