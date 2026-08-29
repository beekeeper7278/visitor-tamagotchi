#pragma once
/* ===========================================================================
 * board_pins.h - Waveshare ESP32-S3-Touch-AMOLED-1.8 **V2**
 *
 *  RULE FOR THIS FILE: nothing goes in here that has not been verified on
 *  the actual V2 board. No values carried over from V1. No values inferred
 *  from "similar" Waveshare boards. A plausible-looking wrong pin number
 *  costs hours on real hardware, so unverified values are left as compile
 *  errors rather than as guesses.
 *
 *  V2 uses CO5300 (display) + CST820 (touch).
 *  If you ever see SH8601 or FT3168 referenced anywhere in this project,
 *  something has drifted to the V1 board and is wrong.
 * ======================================================================== */

/* ===========================================================================
 *  *** FROZEN — PHASE 1 HARDWARE BASELINE, ACCEPTED 2026-08-28 ***
 *
 *  Phase 1 hardware validation PASSED on the physical board and this
 *  configuration is now the known-good baseline. See docs/PHASE1-RESULTS.md
 *  and the git tag phase1-hardware-baseline.
 *
 *  DO NOT CHANGE any of the following without a REPRODUCIBLE HARDWARE
 *  FAILURE that requires it:
 *    - the V2 pin mapping (QSPI 4/5/6/7 + SCLK 11 + CS 12, I2C 15/14)
 *    - display geometry and BSP_LCD_COL_OFFSET
 *    - BSP_LCD_RST = -1 (panel reset runs through the TCA9554 sequence)
 *    - BSP_QSPI_HZ = 40 MHz
 *    - the TCA9554 reset sequence and its timings
 *    - the touch transform
 *    - the IMU axis mapping and signs
 *
 *  These values were measured on hardware, not inferred. "It looks wrong"
 *  is not a reason to change one. If you believe a value is wrong, first
 *  reproduce the failure on the device and record it, then change the value
 *  and re-run the Phase 1 diagnostic to confirm — and update
 *  docs/PHASE1-RESULTS.md in the same commit.
 *
 *  Unverified blocks below (flag == 0) are NOT frozen. Filling one in with a
 *  hardware-verified value is expected work, not a violation of this freeze.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * VERIFIED - supplied as ground truth in the project brief, section 1
 * ------------------------------------------------------------------------ */

#define BSP_I2C_SDA        15
#define BSP_I2C_SCL        14
#define BSP_I2C_HZ         400000

/* Known I2C devices (7-bit addresses) */
#define I2C_ADDR_TOUCH     0x15   /* CST820 capacitive touch      */
#define I2C_ADDR_CODEC     0x18   /* ES8311 audio codec           */
#define I2C_ADDR_IOEXP     0x20   /* TCA9554 IO expander          */
#define I2C_ADDR_PMIC      0x34   /* AXP2101 PMIC                 */
#define I2C_ADDR_RTC       0x51   /* PCF85063 RTC                 */
#define I2C_ADDR_IMU       0x6B   /* QMI8658 IMU                  */
#define I2C_ADDR_UNKNOWN   0x7E   /* UNIDENTIFIED - do not write  */

/* Display geometry - verified */
#define BSP_LCD_W          368
#define BSP_LCD_H          448
#define BSP_LCD_COL_OFFSET 16     /* passed to Arduino_CO5300 as col_offset1 */

/* TCA9554 reset sequence - verified in brief section 2.
 * Bits 0, 1, 2 and 6 are driven LOW -> 20ms -> HIGH -> 50ms before
 * gfx->begin(). Which bit drives which rail is NOT known; we only ever
 * apply them as a group, which is all the verified sequence supports. */
#define BSP_TCA_RESET_MASK 0x47   /* bits 0,1,2,6 = 0b01000111 */
#define BSP_TCA_LOW_MS     20
#define BSP_TCA_HIGH_MS    50

/* TCA9554 register map (standard TI part, not board-specific) */
#define TCA9554_REG_INPUT  0x00
#define TCA9554_REG_OUTPUT 0x01
#define TCA9554_REG_POLINV 0x02
#define TCA9554_REG_CONFIG 0x03   /* 0 = output, 1 = input */

/* ---------------------------------------------------------------------------
 * UNRESOLVED - [NEED VERIFIED]
 *
 * These are NOT guesses-in-waiting. Each one is a value I do not have and
 * will not invent. Fill a block in, flip its _VERIFIED flag to 1, rebuild.
 *
 * Until a flag is 1, the corresponding feature is compiled out and the
 * firmware reports it as unavailable at boot rather than misbehaving.
 * ------------------------------------------------------------------------ */

/* --- (A) QSPI display bus ---  ** VERIFIED 2026-08-28 ** -----------------
 * Arduino_ESP32QSPI(cs, sck, d0, d1, d2, d3)
 *
 * SOURCE: Waveshare's own V2 example,
 *   waveshareteam/ESP32-S3-Touch-AMOLED-1.8
 *   examples/arduino-v2/libraries/Mylibrary/pin_config.h
 * i.e. the CO5300 V2 example, NOT the V1 SH8601 code.
 *
 *   LCD_CS    = GPIO12      LCD_SDIO0 = GPIO4
 *   LCD_SCLK  = GPIO11      LCD_SDIO1 = GPIO5
 *                           LCD_SDIO2 = GPIO6
 *                           LCD_SDIO3 = GPIO7
 *
 * No conflict with I2C (14/15) or the BOOT strap (0).
 */
#define BSP_QSPI_VERIFIED  1
#if BSP_QSPI_VERIFIED
  #define BSP_QSPI_CS      12
  #define BSP_QSPI_SCK     11
  #define BSP_QSPI_D0      4
  #define BSP_QSPI_D1      5
  #define BSP_QSPI_D2      6
  #define BSP_QSPI_D3      7
  /* VALIDATED 2026-08-28 (was [GUESS]). At 40 MHz the measured bus rate is
   * 7.99 MB/s -> 24.2 fps flush-only, 19.0 fps end-to-end, with the panel
   * glitch-free on visual inspection (no tearing, snow or shifted image).
   * FROZEN: do not raise this chasing fps. The end-to-end shortfall is a
   * render-side cost, not a bus shortfall, so a faster clock buys little and
   * risks the signal integrity we just confirmed. */
  #define BSP_QSPI_HZ      40000000
  /* -1 = GFX_NOT_DEFINED. Panel reset is driven through the TCA9554 by the
   * verified pre-begin() sequence, not by a dedicated GPIO. */
  #define BSP_LCD_RST      -1
#endif

/* --- (B) BOOT button ---  ** VERIFIED 2026-08-28 ** -----------------------
 * TESTED ON HARDWARE: GPIO0 reads LOW while BOOT is held and HIGH when
 * released. BOOT = GPIO0, confirmed, not assumed.
 *
 * GPIO0 is ALSO the strapping pin the ROM bootloader samples at reset, so
 * the rules below are permanent, not provisional.
 *
 * STRAPPING RULES that hold regardless of the answer:
 *   - configure INPUT_PULLUP only; never drive it
 *   - never add an external pulldown
 *   - never hold it low across a reset in software
 *   - BOOT-held-at-RESET must keep entering the ROM bootloader (flashing)
 *
 * The Phase 1 diagnostic reads GPIO0 without claiming it is the button, so
 * you can confirm it by pressing BOOT and watching the serial output.
 */
#define BSP_BOOT_BTN_VERIFIED 1
#if BSP_BOOT_BTN_VERIFIED
  #define BSP_BOOT_BTN     0
#endif
/* Kept as the pin the diagnostic observes, so the reporting path is
 * identical whether or not the button has been confirmed. */
#define BSP_BOOT_CANDIDATE BSP_BOOT_BTN

/* Active-low: pressed == LOW. Phase 3 uses a short press to toggle the menu,
 * and must debounce - see BOOT_DEBOUNCE_MS in config.h. */
#define BSP_BOOT_ACTIVE_LOW 1

/* --- (C) CST820 INT / RST -------------------------------------------------
 * Not required for Phase 1: touch is polled over I2C at the LVGL indev rate,
 * which needs no INT line.
 * Consequences of leaving this unresolved:
 *   - no recovery from a wedged touch controller (needs RST)
 *   - no touch-to-wake from sleep (needs INT on an RTC-capable GPIO)
 * Also unknown: whether touch reset is routed through the TCA9554.
 */
#define BSP_TOUCH_INT_VERIFIED 0
#if BSP_TOUCH_INT_VERIFIED
  #define BSP_TOUCH_INT    -1
  #define BSP_TOUCH_RST    -1
#endif

/* --- (D) QMI8658 axis orientation ---  ** VERIFIED 2026-08-28 ** ---------
 * Captured on hardware by the guided 3-pose 'x' diagnostic, not assumed.
 * Result is a clean identity mapping with every sign negated:
 *     screen_axis = -raw_axis, for all three.
 *
 * SCREEN CONVENTION (matches LVGL, and note the Z sense carefully):
 *     +X = screen right
 *     +Y = screen DOWN
 *     +Z = AWAY from the viewer, i.e. INTO the screen
 * Each reads +1g when gravity points that way. So a board lying flat with
 * the screen facing up reads mapped_z = +1g, and X/Y both ~0.
 *
 * The +Z sense is "into the screen", NOT "toward the viewer". An earlier
 * comment in the capture tool said the opposite; the captured NUMBERS were
 * right and the wording was wrong. Corrected here and in diag.cpp so the
 * upside-down check in a later phase is not built on a sign error.
 *
 * Cross-check against measured data (board flat, screen up):
 *     raw ax=-132 ay=+73 az=-8300
 *     mapped x=+0.02g  y=-0.01g  z=+1.01g   <- as expected
 *
 * X and Y are what the tilt maze uses; gravity maps straight to roll
 * direction with no further sign juggling.
 */
#define BSP_IMU_AXES_VERIFIED 1
#if BSP_IMU_AXES_VERIFIED
  #define BSP_IMU_X_SRC    0    /* ax */
  #define BSP_IMU_X_SIGN   -1
  #define BSP_IMU_Y_SRC    1    /* ay */
  #define BSP_IMU_Y_SIGN   -1
  #define BSP_IMU_Z_SRC    2    /* az */
  #define BSP_IMU_Z_SIGN   -1
#endif

/* Still unknown: the IMU INT pin, so no significant-motion wake source. */
#define BSP_IMU_INT_VERIFIED 0

/* --- (E) AXP2101 PMIC -----------------------------------------------------
 * The part generally exposes battery voltage, percentage and charge state,
 * but the rail map for THIS board is unknown. A wrong register write can
 * brown out the panel or misconfigure a rail that keeps the display alive.
 *
 * POLICY: this project performs NO WRITES to 0x34, ever, until the rail map
 * is confirmed. Reads are also gated, because we cannot interpret them
 * safely without knowing the configuration.
 */
#define BSP_PMIC_VERIFIED  0

/* --- (F) ES8311 audio -----------------------------------------------------
 * Needed before audio can be anything but a no-op:
 *   - I2S BCLK / LRCK / DOUT / DIN GPIOs
 *   - MCLK GPIO, or confirmation that MCLK is internally generated
 *   - the PA / amplifier enable line (often a TCA9554 bit on these boards)
 * v1 ships silent by design, so this blocks nothing yet.
 */
#define BSP_AUDIO_VERIFIED 0

/* --- (G) PCF85063 backup power ---  ** VERIFIED 2026-08-28 ** ------------
 * TESTED ON HARDWARE: the RTC keeps time across COMPLETE USB power removal.
 *   before unplug : 2000-01-01 21:06:11
 *   after ~6 min fully unplugged, reconnected:
 *   after         : 2000-01-01 21:12:02
 *   elapsed 5m51s, matching real elapsed time.
 * => a backup cell/supercap IS fitted on this board.
 * => "age advances across sleep, deep sleep and full power-off" (brief
 *    section 4) is ACHIEVABLE on this hardware.
 *
 * Note on the OS (oscillator-stop) flag: it read SET both before and after
 * that test, which is consistent rather than contradictory - the clock had
 * never been set, so the flag had never been cleared. It is a sticky
 * "clock integrity not guaranteed" flag, not a live power-loss indicator.
 * See BSP_RTC_OS_CLEARABLE below.
 */
#define BSP_RTC_BACKUP_VERIFIED 1

/* --- (G2) PCF85063 OS flag clear ---  ** VERIFIED 2026-08-28 ** ----------
 * TESTED ON HARDWARE by the 'o' diagnostic, not assumed:
 *   seconds register 0x96 (OS SET)
 *   -> plain write of (0x96 & 0x7F) with the oscillator left running
 *   -> reads back 0x16, OS CLEARED.
 * No CTRL1 STOP bracket was needed.
 *
 * So OS is a STICKY "clock integrity not guaranteed" flag: hardware sets
 * it when the oscillator stops, and only software clears it. It is NOT a
 * live power-loss indicator. This is why it read SET both before and after
 * the power-cycle test - the clock had simply never been set, so the flag
 * had never been cleared.
 *
 * END-TO-END CONFIRMATION (2026-08-28): after clearing OS at 21:17, the
 * board was FULLY UNPLUGGED and reconnected. OS read back STILL CLEAR and
 * the time had advanced correctly to 21:36:56. So a cleared flag stays
 * cleared as long as the oscillator keeps running, and the oscillator does
 * keep running on battery backup. The flag therefore works exactly as the
 * section 6 integrity signal requires - independently reconfirming (G).
 *
 * Protocol this implies for Phase 5:
 *   - rtc_set()  : write time with OS bit clear, then confirm it read back
 *                  clear. That arms the flag as a real integrity signal.
 *   - on boot    : OS SET  -> the oscillator stopped since we last set the
 *                             clock -> treat time as INVALID, ask the user.
 *                  OS clear-> trust the time.
 * Without the explicit clear on set, the flag is stuck SET forever and
 * carries no information at all.
 */
#define BSP_RTC_OS_CLEARABLE 1
