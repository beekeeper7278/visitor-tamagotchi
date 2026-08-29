# Phase 1 — Hardware Validation Results

**Status: PASS — accepted 2026-08-28.**
Board: Waveshare **ESP32-S3-Touch-AMOLED-1.8 V2** (CO5300 display + CST820 touch).

This file is the record of what was measured on real hardware. Every number
below came off the device, not from a datasheet and not from a similar board.

## Device identity

| Property | Value |
|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2, dual core @ 240 MHz |
| Flash | 16 MB, quad (eFuse), 3.3 V |
| PSRAM | 8 MB embedded (AP_3v3) |
| MAC | `28:84:85:8d:51:68` |
| USB | USB-Serial/JTAG (native), port survives reset |

## Frozen hardware configuration

See the FROZEN block at the top of `include/board_pins.h`. Summary:

| Setting | Value |
|---|---|
| QSPI D0 / D1 / D2 / D3 | 4 / 5 / 6 / 7 |
| QSPI SCLK / CS | 11 / 12 |
| QSPI clock | 40 MHz |
| LCD reset | not connected (`GFX_NOT_DEFINED`, `-1`) — panel reset runs through TCA9554 |
| Panel geometry | 368 x 448, column offset 16 |
| I2C SDA / SCL | 15 / 14 @ 400 kHz |
| BOOT button | GPIO0, active low |
| IMU axis map | screen = -raw on all three axes |

## Measurements

### 1. I2C scan — PASS
Exactly the seven documented addresses, no more, no fewer:

`0x15` CST820 touch · `0x18` ES8311 codec · `0x20` TCA9554 IO expander ·
`0x34` AXP2101 PMIC · `0x51` PCF85063 RTC · `0x6B` QMI8658 IMU ·
`0x7E` UNIDENTIFIED (probed read-only, never written)

TCA9554 reset sequence (bits 0,1,2,6 low → 20 ms → high → 50 ms): applied.

### 2. Display throughput — PASS, with one caveat
Measured through the real LVGL flush path, not a synthetic benchmark.

| Metric | Value |
|---|---|
| Frames rendered | 30 |
| Flush chunks | 240 (8.0 per frame) |
| Pixels pushed | 4,945,920 |
| Time in flush | 1,238,673 us |
| Slowest single chunk | 5,571 us |
| **Bus throughput** | **7.99 MB/s** |
| **Full-frame fps (flush only)** | **24.2** |
| **End-to-end fps (render + flush)** | **19.0** |

**CAVEAT:** end-to-end 19.0 fps is just below the ~20 fps threshold the brief
set for the page-slide animation. Per the brief's own fallback, Phase 3 should
plan for a ~120 ms cross-fade or an instant cut with a page-dot flash rather
than a 368 px horizontal slide. Re-measure once real content replaces the test
card — 19.0 is marginal, not settled.

### 3. LVGL memory high-water — PASS
| Metric | Value |
|---|---|
| `LV_MEM_SIZE` configured | 49,152 bytes |
| LVGL used | 7,392 bytes (16%) |
| LVGL high-water | 3,364 bytes |
| Fragmentation | 0% at boot, 2% steady-state |
| Draw buffer | 44,160 bytes (60 lines, internal SRAM) |
| Internal SRAM free | 237,620 bytes |
| PSRAM used | 4,156 bytes |

**FLAG:** design target for PSRAM in v1 is 0 bytes; actual is 4,156. Small but
non-zero. Not investigated — deliberately left alone under the Phase 1 freeze.

48 KB `LV_MEM_SIZE` is amply sized against a 3,364-byte high-water, but the
test card is far simpler than the real UI. Re-check at Phase 3.

### 4. IMU axis mapping — PASS (verified, not assumed)
QMI8658 `WHO_AM_I = 0x05` (expected), `REVISION = 0x7C`.
Accelerometer +/-4g, 250 Hz, 8192 LSB/g.

Mapping captured by the guided 3-pose routine:

```
screen_axis = -raw_axis, for all three
+X = screen right, +Y = screen DOWN, +Z = INTO the screen
```

Cross-check, board flat and screen up: `x=+0.02 y=-0.00 z=+1.01` — correct.

### 5. BOOT button — PASS
GPIO0 reads LOW held / HIGH released. Configured `INPUT_PULLUP` only, never
driven. BOOT+RESET bootloader entry still works — demonstrated by the
successful uploads in this session, which is the only proof that matters.

### 6. Brightness — PASS
CO5300 command `0x51` via `setBrightness()`. Sweep
`0xFF → 0xC0 → 0x80 → 0x40 → 0x10 → 0x00 → 0xD0` visibly changes the panel.

### 7. RTC + backup power — PASS
PCF85063 keeps time across **complete USB power removal** (verified earlier:
5m51s elapsed unplugged matched real time). A backup cell/supercap is fitted.

The OS (oscillator-stop) flag is software-clearable by a plain write of
`seconds & 0x7F` with the oscillator left running — no CTRL1 STOP bracket
needed. A cleared flag stays cleared across a full power cycle.

**Consequence, and it is a real design constraint:** OS is a *sticky* integrity
flag, not a live power-loss indicator. `rtc_set()` MUST explicitly clear it
after writing the time, or the flag stays SET forever and carries no
information at all. Boot logic: OS set → treat time as INVALID and ask the
user; OS clear → trust it.

At capture time the RTC read `2000-01-02 02:04:58` — running, but never set to
real wall-clock time. Setting it is Phase 5 work.

### Storage foundation — PASS
Landed in Phase 1 deliberately (brief's deviation from the original build
order) so every later phase persists from birth.

| Metric | Value |
|---|---|
| `sizeof(save_t)` | 332 bytes (budget 384) |
| Schema version | 1 |
| Journal | 24 entries x 8 bytes |
| Load | OK |
| Write / read-back | MATCH |
| Shadow-compare | identical blob correctly skipped flash |
| CRC damage detection | yes |

### Runtime stability
Heartbeat over several minutes: `lvgl_used` flat at 7,620 bytes, high-water
unchanged at 3,364, fragmentation steady at 2%, internal free flat at 237,620.
No leak, no drift.

## Visual checks confirmed on the panel

- Corner markers all four visible and equally inset → column offset 16 correct
- RGB swatches read red/green/blue left→right → RGB565 byte order correct
- No tearing, snow, or shifted image at 40 MHz QSPI
- Touch dot tracks the finger; coordinates in range and correctly oriented
- Brightness sweep visibly changes the panel

## Flash procedure used

Normal `pio run -t upload`. **No factory or full-chip erase.** Only the four
written sector ranges were erased: `0x0` bootloader, `0x8000` partition table,
`0xe000` otadata, `0x10000–0xb4fff` app0. NVS at `0x9000–0xdfff` was never
touched, and the storage self-test confirmed saved state survived.

Keep it this way. A full erase destroys the pet.
