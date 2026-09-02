# Visitor

A virtual pet for the **Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)**.

A small creature arrives from somewhere else, stays with you for a week or
two, and then goes home. While it is here you feed it, clean up after it,
put it to bed, play four games with it, and tell it off when it misbehaves.
It grows through four life stages, develops a personality, remembers what
happened, talks to you in a recorded voice, and leaves you a handwritten note
when it goes.

It is built for a child. That constraint drives most of the design decisions
in this repository: large touch targets, no reading required for the core
loop, nothing that scolds, and a departure that is a consequence rather than
a punishment.

> **Status: pre-release.** The current recommended build is
> [`v1.0.0-pre.2`](../../releases/tag/v1.0.0-pre.2). See
> [Known limitations](#known-limitations) — they are real and listed honestly.

---

## Hardware

| | |
|---|---|
| Board | Waveshare ESP32-S3-Touch-AMOLED-1.8 **V2** |
| Display | 368 x 448 AMOLED, CO5300 over QSPI |
| Touch | CST820 |
| Audio | ES8311 codec + speaker |
| IMU | QMI8658 |
| RTC | PCF85063A |
| PMIC | AXP2101 |
| Flash | 16 MB, custom partition table (no OTA) |

**V2 specifically.** The V1 board uses different parts (a different display
controller and touch IC) and this firmware will not run on it. The pin map,
display geometry, touch transform and IMU axis mapping in
`include/board_pins.h` are verified against real hardware and carry a FROZEN
banner — do not change them without a reproducible hardware failure.

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
git clone <this repo>
cd visitor-tamagotchi
pio run                 # build
pio run -t upload       # flash the firmware over USB
```

That is enough to get a running Visitor with the procedural chirp voice.

### The voice packs (optional but recommended)

The Visitor speaks recorded lines. The packs are ~4 MB each, are generated
artifacts, and are **deliberately not in git** — they are fully reproducible
from the build script plus two public-domain Piper models. Without them the
firmware falls back to a procedural chirp voice, which is a supported
degraded mode rather than a failure.

To build and flash them, follow
[`tools/voicepack/README.md`](tools/voicepack/README.md), then:

```bash
pio run -t uploadfs     # ~80 s; does NOT touch NVS, so the pet survives
```

### Flashing rules worth knowing

- **Never do a full chip erase.** NVS holds the live pet, its visit history,
  its game records and the device settings. `pio run -t upload` only erases
  the sectors it writes and leaves NVS alone; `erase_flash` destroys the pet.
- **Never drive DTR/RTS on this board, and never issue a bare esptool reset.**
  Doing so has twice wedged the USB-JTAG interface hard enough to need a
  physical unplug. Reboot only through a full `pio run -t upload` or a power
  cycle. `docs/HANDOFF.md` §2b has the full account.

## Serial console

Almost everything is inspectable at 115200 baud. Press `?` for the command
list. A few of the more useful ones:

```
?  help                 *  age / clock report      <  explain evolution
R  care state           >  discipline report       $  departure report
a  game records         J  visit records           TAB  extra menus
```

`TAB` opens a second layer covering audio, voice packs, the PMIC probe,
tilt calibration and the save backup/restore tools.

## Project layout

```
src/            firmware
include/        config.h (all tuning) and board_pins.h (FROZEN hardware map)
tools/voicepack/ Piper voice-pack generator, coverage checker, audition tool
tools/evosurvey/ offline harness that links the SHIPPED evolution code
tools/stagejump/ offline harness for stage-boundary behaviour
docs/           requirements and a detailed engineering handoff
```

`include/config.h` is where the tuning lives — care rates, stage boundaries,
evolution thresholds, discipline frequency, layout geometry. Most numbers in
it are accompanied by the reasoning and the measurement that produced them.

`docs/HANDOFF.md` is long and is the real documentation: what each phase
added, which decisions are load-bearing, what has been verified on hardware
and what has not.

## Known limitations

Stated plainly, because a pre-release that hides them is not useful:

- **Not fully verified on hardware.** Most of the v1 sweep was tested on a
  device, but three things were not: the battery indicator has not been
  looked at on the panel, a Baby daytime nap has not been watched since the
  sleep fix, and normal genuine powered-off elapsed time has not been
  re-verified since the clock-correction work. `docs/HANDOFF.md` §9 lists
  exactly what is outstanding.
- **No charging indicator.** Battery percentage is read from the AXP2101's
  VBAT ADC and is real, but which status bit tracks external power is not
  verified on this board, so it is deliberately not interpreted rather than
  guessed at.
- **Runtime-assembled text is not spoken.** The farewell note, the "new
  favourite game" line and the Journal's About Me are built at runtime and
  differ every visit, so they cannot be prerendered. They use the chirp
  voice. Every one of the 321 fixed lines is recorded in both voices.
- **The AXP2101 is otherwise untouched.** This project performs no writes to
  the PMIC at all, because the rail map for this board is unknown and a wrong
  write can brown out the panel.
- **No networking.** There is no WiFi or Bluetooth code. The clock is set by
  hand on the device; there is no time sync.
- **Single-device project.** It has been developed and tested against one
  board. Behaviour on another unit of the same model should be fine but has
  not been confirmed.

## Licence

**No licence has been chosen yet.** Until a `LICENSE` file is added, default
copyright applies and no permissions are granted for reuse. This needs
resolving before the project is genuinely usable by anyone else.

## Credits

Voices are rendered with [Piper](https://github.com/rhasspy/piper) using the
`en_US-norman-medium` and `en_US-kristin-medium` models, both trained on
public-domain LibriVox recordings — public-domain training data was a
deliberate requirement so the result could actually ship.
