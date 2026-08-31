# Audio, voice and IMU personality — requirements and outcome

**STATUS: IMPLEMENTED, VERIFIED ON HARDWARE, ACCEPTED 2026-08-31.**
Tagged `phase10-feature-baseline`.

Requirements were captured 2026-08-28, when audio was hardware-blocked and
v1 was expected to ship silent. This document now records both the original
requirement and what was actually built, including the compromises.

---

## 1. Volume control — DELIVERED AS SPECIFIED

Four user-accessible settings: **Mute / Low / Medium / High**, on the Settings
page, as a card that shows its current value in the label so "is it muted?" is
answered by looking. Tap cycles.

**Mute silences sound only.** All speech bubbles and every other piece of
visual feedback continue exactly as before. Mute must never be a way to end up
with a pet that cannot communicate — a parent may well leave the device muted
permanently.

**Implemented harder than specified:** mute is **digital zero at the source**,
not codec attenuation, so there is no residual hiss or leak. Verified by ear
at acceptance, for both effects and speech.

The one deliberate exception: the bring-up test tone bypasses the mute gate,
so "you are muted" can never be mistaken for "nothing is wired up".

**[RESOLVED]** the open question of which page hosts the control. It is the
Settings page (renamed from Pet Info in Phase 9.5), alongside Recalibrate Tilt
and Gravity Reactions.

## 2. Sleep behaviour — DELIVERED, AND HARDENED DURING VERIFICATION

The requirement, restated because it is the one most likely to be lost:

> **Normal night-time sleep is quiet.** A Baby taking a *daytime nap* may make
> one or two cute snoring sounds occasionally. There must be **no repetitive
> all-night snoring.** This device will sit in a child's bedroom overnight. A
> charming snore at 2 pm is a serious problem at 2 am.

Implemented as: asleep **and** the latched period is a nap **and** the CLOCK
currently says daytime-nap **and** the stage is Baby **and** budget remains
(max 2 per nap, randomised 45–90 s gap) **and** not muted.

**The clock term was added during verification and is load-bearing.**
`s_sleep_was_nap` is latched on sleep ENTRY, and entry is SKIPPED when the
Visitor is already asleep — so a nap carrying across into the evening left the
flag still reading "nap" at 20:30, and a Baby would have snored all night.
This was observed on hardware (`was_nap 1` at night); only an exhausted budget
happened to be keeping it quiet. Night is now silent **by construction**
rather than by luck.

`care_report()` prints the whole gate, so the requirement is checkable in one
keystroke instead of by sitting up and listening.

## 3. Candidate sounds — ALL DELIVERED

footsteps · eating · cleaning and bathroom effects · happy/sad/reaction
chirps · game sounds · menu feedback · sleep/wake — plus hatch countdown and
chime, evolve, discipline, and the motion reactions.

Two rate limits exist because the naive wiring buzzes rather than sounds:
**footsteps** are limited inside the walk animation (it runs every frame), and
the **maze wall bump** is limited because a player hugging a wall is
legitimately blocked on every frame.

**Memory is the one game that needed real work.** It wants a DISTINCT pitch
per pad, reproduced when the player taps, or the sequence is unhearable — so
the pads get a major arpeggio and bypass `games_sfx()`. A wrong tap sounds its
OWN note first, then the verdict, so a wrong note is audible AS a wrong note.

## 4. Hardware status — UNBLOCKED

`BSP_AUDIO_VERIFIED` is now **1**. Everything listed as unknown in the
original requirement is established and verified on this board:

    MCLK 16 (a real pin, 256x fs)   BCLK 9   WS/LRCK 45
    DOUT 8 (ESP32 -> codec)         DIN 10 (mic -> ESP32)
    PA enable GPIO 46 — a PLAIN GPIO, HIGH = on, NOT a TCA9554 bit
    ES8311 @ 0x18, 16000 Hz

Source: waveshareteam/ESP32-S3-Touch-AMOLED-1.8, **arduino-v2** tree. Trusted
because the same file states our already-frozen display and I2C pins byte for
byte. Note `pin_config.h` contains two naming blocks that DISAGREE —
`DOPIN`/`DIPIN` are swapped relative to `I2S_DO_IO`/`I2S_DI_IO`; the working
example uses the `I2S_*` names.

---

## 5. What was added beyond the original requirement

### Recorded speech, and why the requirement changed

The original plan was a procedural voice; on-device TTS was ruled out as
sounding like a fax machine. The procedural voice went through two
generations — pitched tone bursts, then a formant synth with jitter, glides,
consonants and declination. The second genuinely sounds like a mouth.

It still could not say **"I did a mess. It's art."**, because formant babble
contains no WORDS. That is inherent, not a tuning failure. The report from the
sofa — "a bunch of beeps and boops" — was exactly right. **Understandable
meant recorded.**

Two packs, one per gender: Norman (boy) and Kristin (girl), both Piper models
trained on LibriVox recordings and both **public domain** — a deliberate
requirement, since Apple's system voices sound fine but could never ship.
Both stay mounted; gender picks per line, with no reload or reboot when a
Visitor hatches the other way. Gender remains PRESENTATION ONLY.

### COMPROMISE: the chirp voice survives as the fallback

This is the main compromise and it is deliberate. Lines are keyed by a hash of
their text; a line with no clip falls back to the chirp voice. That path earns
its place three times over:

1. **Dynamic text can never be prerendered.** The farewell note is assembled
   at runtime from what actually happened during the visit, so it will always
   chirp. This is a permanent, shipping behaviour, not a gap.
2. **A board with no pack flashed still has a Visitor that vocalises.** The
   packs are ~4 MB generated artifacts and are NOT in git, so a fresh clone
   genuinely runs this way until `pio run -t uploadfs`.
3. **Wordless moments want a noise, not a sentence.**

Accepted by ear at verification: chirps are charming rather than broken, which
was the bar.

### COMPROMISE: one asset set serves four stages

Four separate recordings would have cost four times the flash to say the same
words. Playback is resampled instead, which raises pitch and tempo together
the way a smaller creature actually sounds. The consequence is that the stage
ladder must stay **narrow — 7% total** (Baby 1.00 → Adult 0.93) — because a
wider one would leave the Adult speaking noticeably slower than the Baby, and
the pace was tuned for intelligibility. Accepted as reading like one character
growing up.

### Repartitioning

16 kHz did not fit. The stock table reserved two 6.5 MB OTA app slots; this
device is flashed over USB by hand and has no OTA path, so the second slot was
dead flash. Filesystem 3.37 MB → 11.88 MB, app capped at 4 MB so an oversized
firmware fails the BUILD rather than the flash. **NVS untouched at 0x9000 size
0x5000**, verified by dumping the Visitor before and after.

### IMU personality and tilt calibration

Gravity slide (0.15 g dead zone), upside-down (z < −0.65 held 1.2 s, relief
only if it lasted > 2.5 s), shake as a JERK rather than a large reading, and
an annoyance meter that is **FLAVOUR ONLY** — no accumulator, no form choice,
no evolution, no visit quality. A backpack cannot damage a Visitor.

Calibration rejects a capture taken while moving (0.18 g spread over 1.4 s).
A calibration recorded mid-wave silently tilts every future reading and would
be experienced as a broken Visitor rather than a bad capture.

**Gravity Reactions OFF gates AMBIENT behaviour only** — the IMU still reads,
calibration still runs, and Tilt Maze is untouched. Verified, and structural:
the event counters sit downstream of the `blocked` early-return.

No BSP or IMU changes. Motion reads through `diag_imu_read_screen()`, the one
accessor applying the FROZEN raw→screen transform, then through the same
display-frame adapter Tilt Maze established. Calibration is a user offset
applied on top; the frozen signs are untouched.

---

## 6. Verification

Six physical sections (gravity, upside down, shake, calibration, gravity-off,
audio) and sixteen automated checks, all passed. Two defects found and fixed:
the night-snore leak above, and a voice coverage sweep that counted only its
failures and so reported "21 checked" for a run of over a thousand lookups.

Not verified, and why: **farewell / new Visitor** (necessarily ends the
Visitor), and the **schema migration fixtures** (they overwrite the real save
with a fake blob and never restore it). The latter is covered structurally —
Phase 10 touches neither `storage.*` nor `pet.h`, and every boot re-confirms
`sizeof(save_t) 433 / schema 8 / load OK`.

Full detail in `docs/HANDOFF.md` §2e.
