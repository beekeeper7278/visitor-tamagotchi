# Visitor 👾

**Visitor** is a virtual pet for the **Waveshare ESP32-S3-Touch-AMOLED-1.8 V2**.

A little creature arrives to stay with you. Feed it, play games, clean up after it, help it sleep, and teach it how to behave. As time passes, Visitor grows from Baby → Kid → Teen → Adult and develops its own personality, favorite game, habits, memories, and evolution form.

Eventually every Visitor goes home — but its visit is remembered, and future Visitors may even have heard about the ones that came before.

> **Status:** Pre-release  
> **Recommended version:** `v1.0.0-pre.2`

## Features

- 12 evolution forms
- Real-time aging — **1 real day = 1 Visitor year**
- Persistent RTC-based life while powered off
- Boy / Girl / Surprise identity
- Multiple personalities and care-driven behavior
- Hunger, happiness, cleanliness, discipline, energy, and weight
- Daytime naps and nighttime sleep
- Dreams and a personal Journal
- Dynamic favorite games that can change over time
- Four built-in games:
  - Higher / Lower
  - Reaction / Tag
  - Memory
  - Tilt Maze
- Gravity, shake, and upside-down reactions
- Piper Boy and Girl voice packs
- Battery percentage display
- Visit Records and personalized farewells
- No death mechanic — neglected Visitors may become grumpy, scruffy, mischievous, or chonky, but never die

## Hardware

Designed specifically for:

**Waveshare ESP32-S3-Touch-AMOLED-1.8 V2**

- ESP32-S3R8
- 368×448 AMOLED
- CO5300 display controller
- CST820 touch
- QMI8658 IMU
- PCF85063A RTC
- ES8311 audio codec
- AXP2101 PMIC
- 16 MB flash / 8 MB PSRAM

**The V1 board is not supported.**

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/beekeeper7278/visitor-tamagotchi.git
cd visitor-tamagotchi

pio run
pio run -t upload```

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

[MIT](LICENSE). Build it, modify it, learn from it, redistribute your own
version — commercially or otherwise. Keep the copyright notice with it, and
understand it comes with no warranty.

The voice models it uses were chosen to match: both are trained on
public-domain LibriVox recordings, so a pack you build is yours to ship.

## Credits

Voices are rendered with [Piper](https://github.com/rhasspy/piper) using the
`en_US-norman-medium` and `en_US-kristin-medium` models, both trained on
public-domain LibriVox recordings — public-domain training data was a
deliberate requirement so the result could actually ship.
