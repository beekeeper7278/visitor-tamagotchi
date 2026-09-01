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

/* The SAME instant, written the way a parent reads it: "Aug 29, 2026  7:30 PM".
 * The ISO form above is for the console and the logs; this is what goes on a
 * screen a five-year-old is standing in front of. 12-hour with AM/PM for the
 * same reason setclock edits in 12-hour. Writes "not set yet" for ts == 0. */
void rtc_format_friendly(uint32_t ts, char *buf, size_t len);

/* Move an RTC-anchored timestamp by a CLOCK CORRECTION.
 *
 * Every timestamp this project persists - hatch_ts, last_sim_ts,
 * egg_hatch_ts, depart_due_ts, the journal's dates, the game-streak stamp -
 * is an absolute reading of THIS clock. Correcting the clock therefore
 * invalidates all of them together, and the repair is to move them all by
 * the same delta. See sim_clock_corrected(), which is the only caller that
 * should ever need this.
 *
 * SATURATES rather than wraps. These are unsigned, and a large backward
 * correction on a small value would wrap to a date tens of thousands of
 * years in the future - which reads as valid everywhere and is impossible to
 * notice. 0 means "not set" and is always left as 0. */
uint32_t rtc_shift_ts(uint32_t ts, int32_t delta);

/* Unix seconds -> broken-down civil time, and back. Exposed because the
 * setter, the pre-hatch card and the console clock tools all need to build
 * an rtc_time_t from an instant, and three private copies of a
 * civil-from-days routine is how two of them end up disagreeing. */
void     rtc_from_unix(uint32_t ts, rtc_time_t *out);
uint32_t rtc_to_unix(const rtc_time_t *t);

/* THE FIRMWARE BUILD STAMP, as a starting point for a clock that has never
 * been set.
 *
 * A hardcoded date is the wrong default for an unset RTC: it is plausible,
 * so the clock accepts it and rtc_trusted() goes true, and then it is wrong
 * forever while looking entirely correct. The build stamp is not arbitrary,
 * is never in the future, moves every time the device is flashed, and on a
 * freshly built device is within days of the truth - so a parent adjusts
 * rather than dialling in six fields from nothing.
 *
 * It is a SEED, never a confirmation. Nothing reaches the RTC and nothing is
 * marked confirmed until a human presses Confirm.
 *
 * Caveat worth knowing: __DATE__ is fixed when THIS FILE is compiled, so an
 * incremental build that does not recompile rtc.cpp keeps the older stamp.
 * That only ever makes the seed staler, never later than now, which is the
 * safe direction. */
void rtc_build_stamp(rtc_time_t *out);

const char *rtc_health_name(rtc_health_t h);

#ifdef __cplusplus
}
#endif
