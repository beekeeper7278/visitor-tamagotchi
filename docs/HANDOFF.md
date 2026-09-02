# Visitor — Continuation / Handoff

Rewritten 2026-08-31 at the end of Phase 10.
Updated 2026-09-01 for the v1.0.0 pre-release — see §0, §1 and §2f.

---

## 0. Status

**The v1 release-candidate bug sweep has STARTED and is IN PROGRESS.**
Committed and pushed on `wip/phase8-9-pacing`, and flashed:

- `c0603c7` four defects found by the sweep (§2f)
- `311f2b9` Date & Time before the hatch, safe clock correction (§2f)
- the mischief-frequency halving (§2g)
- four polish items: dynamic favourite game, the stale-Sleepy fix, a
  sleeping Visitor refusing all food, and a battery indicator (§2h)

**Test 1 of the six PASSES on hardware** — see §9, which carries the
measurement. The rest are still outstanding, and under the phase gate that
means the sweep is not finished.

The user calls this state **"v1.0.0 pre-release"**. It is NOT tagged `v1.0.0`
and NOT merged to `main`; both need asking for.

**Phase 10 (audio, voice packs, IMU personality, tilt calibration, settings)
is COMPLETE and ACCEPTED on hardware.** The tag `phase10-feature-baseline`
covers it together with the two defects the verification pass found and fixed
(see §2e). Full detail in §2e and `docs/PHASE10-AUDIO-REQUIREMENTS.md`.

Phase 9.5 remains COMPLETE and ACCEPTED, tagged `phase9.5-polish-baseline`.

| Phase | State |
|---|---|
| 1 Hardware baseline | COMPLETE, tagged |
| 2 Pet renderer / animation / bubbles | COMPLETE, tagged |
| 3+4 Menu, pages, food, care, bathroom, messes | COMPLETE, tagged |
| 5+6 RTC, aging, sleep, persistence, offline catch-up | COMPLETE, tagged |
| 7 All four games | COMPLETE, tagged |
| 8 Evolution / personality / discipline | COMPLETE, tagged |
| 9 Journal / Visit Records / farewell / egg | COMPLETE, tagged |
| Pacing & balance pass | COMPLETE, tagged `pacing-balance-baseline` |
| 9.5 Personality / dreams / identity / refinement | COMPLETE, tagged `phase9.5-polish-baseline` |
| **10 IMU personality, tilt calibration, audio, voice** | **COMPLETE, tagged `phase10-feature-baseline`** |
| **v1 bug sweep / v1.0.0 pre-release** | **IN PROGRESS — committed and flashed, NOT yet accepted on hardware** |

Nothing is outstanding for Phase 10.

**Do not tag v1.0.0 and do not merge to `main` without being asked.**

## 1. Git

Branch `wip/phase8-9-pacing`, pushed. **A remote DOES exist** — the older
wording in this file claiming "nothing has ever been pushed / no remote
configured" was stale and is corrected here.

    origin      https://github.com/beekeeper7278/visitor-tamagotchi.git
    old-origin  https://github.com/beekeeper7278/tamagotghi.git   (previous repo)

    HEAD     311f2b9  v1.0.0 pre-release: establish the clock BEFORE the
                      hatch, and never replay a clock correction as elapsed
                      life                                          [§2f]
    c0603c7  v1 bug sweep: four defects, and the harness that measured
             them                                                   [§2f]
    5ee14da (tag: phase10-feature-baseline)  Phase 10 verification: two fixes
                                             and the diagnostics that found them
    ab1f5f2  Phase 10: the Visitor speaks - Piper voice packs, one per gender
    3361bb1  Phase 10: wire audio through gameplay; formant synth voice
    1c1ec25  Phase 10: settings record, tilt calibration, IMU personality
    4eb8cdc  Phase 10: ES8311 audio bring-up, verified on hardware
    7d13503 (tag: pre-phase10-baseline)      Hatch countdown drop
    305c2b7 (tag: phase9.5-polish-baseline)
    496cf0c (tag: pacing-balance-baseline)

`main` is still at `e8726d0`. The phase gate stands: work is TAGGED only
after the user accepts it on hardware. Ask before committing; do not merge to
`main` unilaterally.

**A safety branch exists.** `safety/v1.0.0-pre-release-2026-09-01` points at
`311f2b9` and is pushed. It is a fixed snapshot taken because the sweep had
accumulated ~350 lines of uncommitted work that existed only in one working
tree; it is not a working branch and should not move. Delete it once the
sweep is accepted and tagged.

The two sweep commits are deliberately SEPARATE, and the split cost some
effort worth not undoing: `sim.cpp` and `diag.cpp` contain work from both, so
they were split at hunk level, and `c0603c7` was built standalone in a
throwaway worktree to prove the history bisects cleanly rather than carrying
a broken intermediate. Either commit can be reverted without the other, which
is what the phase gate needs when only one of them turns out to be wrong.

`pre-phase10-baseline` (`7d13503`) is the clean rollback point before any
Phase 10 work.

## 2. What Phase 9.5 added

**Settings page.** "Pet Info" is renamed to Settings and is now the device's
home for Date & Time, the Visitor's age/stage/form/weight/gender, and three
RESERVED, visibly-disabled cards that Phase 10 owns: Volume, Recalibrate
Tilt, Gravity Reactions.

**Age copy: 1 real day = 1 Visitor year**, said out loud in the child-facing
UI ("6 years old", not "day 6"). Diagnostics keep printing day numbers. Visit
Records keep *days* because that is a duration, and now say "stayed N days".

**Surprise colour and gender.** "Random" is now "Surprise" everywhere the
player can see it. Seven colours (Red, Purple, Blue, Green, Teal, Yellow,
Surprise) and three genders (Boy, Girl, Surprise). The Surprise swatch is
drawn as the six colours in stripes, and the egg shows a rainbow shell so the
resolved colour stays hidden until the hatch. **Both surprises resolve once,
at START, and are persisted immediately with a forced save** — a power cut
during the five-minute hatch brings back the same egg and the same Visitor.
Verified: resolved to palette 4 / girl, rebooted mid-hatch, came back
identical.

**Gender is presentation only** and is held to the same standing rule as the
egg colour: it never reaches an accumulator, a form choice, a care rate, a
discipline roll or the evolution path.

**Hatch reveal, then first words.** Three beats, strictly ordered and never
overlapping: the shell opens; *if* gender was a Surprise a banner reads "It's
a girl!" for 3.2 s (a banner, not a bubble — bubbles are preemptible and
cooldown-gated, and this moment must not be refused or talked over); then the
Visitor's own first words. If there is a previous Visitor, its callback
follows as a second beat rather than replacing the greeting. If the reveal is
missed, the Journal's first card opens with it.

**Journal "How I grew up" fixed.** `evo_path[]` had one writer, in the
*offline* path, guarded by `if (p->stage < 4)` — and `STAGE_ADULT` **is** 4.
So a device left switched on recorded nothing, and even an offline evolution
could never record the final adult form. `pet_record_form()` now writes from
every path; old saves are backfilled with what is *knowable* only. Verified
live: `Baby -> Good Kid -> Rowdy Teen -> Grumpy Adult`, surviving reboots.

**First-person dreams and About Me.** See §4 for the dream rules. "About Me"
is assembled in the Visitor's own voice from what actually happened —
personality, favourites, discipline history, one specific funny detail, and
how it turned out.

**Deferred menu reactions.** `ui_bubble_say()` refuses everything while a
menu or game is open, which is right for chatter and wrong for a reaction the
player deliberately triggered — the light switch is on the Care page, so the
answer was spoken to a covered screen and expired behind the menu.
`ui_bubble_say_deferred()` queues it and shows it when the pet screen returns,
**with its duration starting then**. Three fixed slots, oldest dropped,
120 s expiry, dropped entirely at the farewell.

**Personality-specific dialogue.** All lines moved into `dialogue.cpp`.
Covers dreams, lights on/off, old messes, waking, being poked asleep,
mischief, being told off, food, after a game, and first words. Flavoured by
both traits *and* form. Every selector falls back to a generic pool; a trait
pool wins 70% of the time, not always. Grumpy is grumbly, never cruel.

**Discipline frequency and history.** Stage-weighted, required order
Kid > Baby > Teen > Adult (100/85/62/40 %), applied as a multiplier on the
finished percentage so the ordering survives every other term. Babies are no
longer excluded. `learned_mischief` is a rolling EMA over how windows were
*resolved* — corrected pulls toward 0, ignored toward 100 — so early
discipline genuinely calms a Teen and **nothing is ever locked in**. Measured
on hardware: a well-raised Kid rolls 9%, a neglected one 27%. Rolls every
15 s with a randomised 60–180 s gap after each opportunity. A settling hold
suppresses spontaneous mischief for 2 min after a hatch and 1 min after boot.

## 2b. FROZEN — hardware, do not touch

`include/board_pins.h` carries a FROZEN banner. **Do not change** the V2 pin
map (QSPI D0–D3 = 4/5/6/7, SCLK 11, CS 12, I2C SDA 15 / SCL 14), the 40 MHz
QSPI clock, `BSP_LCD_RST = -1`, the 368x448 geometry, the column offset 16,
the TCA9554 reset sequence, the touch transform, or the IMU axis mapping —
unless a **reproducible hardware failure** demands it, and then re-run the
Phase 1 diagnostic and update `docs/PHASE1-RESULTS.md` in the same commit.

**USB-JTAG: NEVER USE A BARE RTS RESET.** Twice in one session the board
stopped answering entirely — no console output, and esptool unable to connect
with any `--before` mode — while still enumerating correctly (Espressif "USB
JTAG/serial debug unit", 303A:1001, MAC 28:84:85:8D:51:68). Both wedges
immediately followed a *bare* RTS-based reset on the native USB-Serial/JTAG
interface: once from a pyserial RTS toggle in a helper script, once from
`esptool --after hard-reset flash-id` used as a cheap reboot. A reset issued
as part of a full `pio run -t upload` has never caused it, across roughly
twenty uploads.

    RULE: never drive DTR/RTS on this board, and never issue a bare esptool
    reset. Reboot ONLY through a full `pio run -t upload`, or a physical
    power cycle.

Recovery needs a physical USB unplug/replug; on the first occasion a cable
swap was also needed, so a marginal cable is a contributing factor. Symptom
to recognise: the port node exists and opens, `list-ports` shows the right
MAC, and absolutely nothing comes back.

Other rules that are easy to break by accident:

- **Never full-erase NVS.** Use plain `pio run -t upload`. A factory erase
  destroys the pet. Normal upload only erases the sectors it writes; NVS at
  `0x9000–0xdfff` is untouched.
- **The tilt maze uses a display-frame adapter over the frozen IMU mapping**
  (`games.cpp`, `mz_step`): `tilt_right = gy`, `tilt_down = -gx`. This is a
  swap AND a vertical negation, determined on hardware. The tidy "pure 90°
  rotation" theory was wrong. Do not "simplify" it and do not edit the frozen
  signs in `board_pins.h` instead.
- `diag_storage_report()` must stay non-destructive. It used to write test
  values into the one real save slot on every boot, destroying the pet. It
  now snapshots and restores. Do not undo that.

## 2c. Tilt Maze regression fix (after Phase 9.5 acceptance)

**Wall sticking.** The collision test moved a whole frame's distance in one
go, sampled ONE line of the Visitor's box per axis, and snapped back to the
previous position when that failed. Three compounding faults: it tested
`nx + MZ_BALL`, the pixel PAST the right edge, so the Visitor was blocked
with a pixel of clearance still left; it sampled only the centre line, so
corners were invisible and a corner could slip into a wall unnoticed; and
once even slightly embedded it asked only "is the new position blocked?",
never "is this an improvement?", so every candidate move was refused and the
player was stuck for good.

The fix keeps the axis separation, which was right and is what lets a blocked
X still slide along Y. `mz_blocked()` is a real box test over every cell the
Visitor overlaps, with inclusive edges. Movement is sub-stepped a pixel at a
time and stops at the last free position, so a fast tilt cannot tunnel or
embed and the Visitor rests flush against the wall. A frame that somehow
begins overlapping moves freely once, so nothing can be trapped permanently.

Measured with the on-device sweep (`` ` ``), which drives the SHIPPED
collision code at full speed into every wall and corner of all sixteen mazes:

    old code : 8528 launches, 1615 embedded, 1172 TRAPPED   -> FAIL
    fixed    : 8528 launches,    0 embedded,    0 trapped   -> PASS

Roughly one hard push into a wall in seven used to leave the player stuck.
The sweep was checked against the old code before being trusted.

**Baby maze simplified.** Baby was structurally a Kid maze - the same Prim's
skeleton with the hazards removed: 6-9 dead ends, every corridor one cell
wide. It is now a wide, branchless path. Kid's 12-16 dead ends were also not
"a few", and Teen was Kid plus holes with an identical skeleton, so the whole
ladder was re-authored. Every figure is measured by the offline validator and
asserted before the templates are pasted in:

    tier    route   dead ends   holes   corridors
    Baby    26-30       0         0     ALL two cells wide
    Kid       30        5         0     one cell
    Teen      30       10         6     one cell
    Adult     34      11-15      11     one cell

Kid and Teen were thinned by removing whole dead-end BRANCHES - never a cell
on the solution route, never a branch containing a hazard - so route lengths
and hole counts are untouched. Adult is unchanged and remains the hardest.
Within a tier every variant shares its route length and (Kid, Teen) its exact
dead-end count, so repeated plays change the SHAPE and nothing else;
difficulty does not drift in either direction.

**No BSP or IMU changes.** `board_pins.h` and `bsp.*` are untouched, and the
display-frame adapter in `mz_step()` (`tilt_right = gy`, `tilt_down = -gx`)
is byte-identical.

New console keys: `` ` `` collision sweep, `\` press the open game's Start,
`'` place the Visitor on the maze exit so the real win path runs. The last
two exist because `games_launch()` only opens the intro screen, so without
them no game round could be reached from the console at all.

## 2d. Hatch-countdown layout [presentation only]

`EGG_ROOT_Y` (-6) exists because the SELECTOR screen needs the room: the
Date & Time card, a colour row, a gender row and START fill everything below
154. (That was TWO colour rows and no date card until the v1.0.0 pre-release
re-derived the layout — see §2f. `EGG_ROOT_Y` itself is unchanged, which is
why everything in this section still holds.) The moment START is pressed all
of that disappears, and the shell was left stranded under the HUD with three
quarters of the panel empty below it.

The countdown now DROPS the egg to `EGG_HATCH_ROOT_Y`, and the label comes
down with it out of the HUD row:

    HUD row          0 .. ~30    (mood dot left, menu handle right)
    countdown label  142 .. 166
    gap                          166 -> 186 = 20 px
    egg shell        186 .. 304  (root 150 + 36 in-box, 118 tall)

142 px clear above, 144 below - centred to 2 px, and every gap is
static_asserted in `scr_main.cpp` beside the selector assertions. One of
those assertions ties `EGG_SHELL_TOP_IN_BOX` back to what `layout_egg()`
actually does, so the margins cannot end up measured against a shell that is
not there.

**The drop target is `PET_HOME_Y`, not a computed "middle", and that is the
load-bearing choice**: it is the exact spot the Baby occupies when the shell
opens, so there is nothing left to jump. Before this, `ui_pet_set_egg(false)`
left `s_pos_y` at -6 and a newly hatched Baby appeared in the HUD and wandered
down from there.

**The non-obvious part.** `scr_main_egg_refresh()` calls `ui_pet_set_egg()`
once a second, and that function used to re-seed `s_pos_y = EGG_ROOT_Y`
unconditionally. Harmless while the egg had one fixed position - and fatal to
any animation, because it would drag the shell back to the top every second
and the drop would never visibly move. It now seeds the position only on the
TRANSITION into egg mode. If a future change makes the egg move again, this
is the line that will catch it.

`ui_pet_egg_drop(animate)`: START animates over `EGG_DROP_MS` (700 ms,
smoothstep); a boot that resumes a hatch already under way passes false and is
placed directly, so resuming looks like the screen was never away.

Measured on hardware: choosing y=-6; after START 4 -> 60 -> 131 -> 150;
reboot mid-hatch snaps to 150 with no second drop; the Baby appears at 150 and
wanders from there. Hatch duration, colour/gender selection, Surprise
resolution, persistence and the reveal sequencing are all untouched.

## 2e. What Phase 10 added — COMPLETE, ACCEPTED, tagged

Four feature commits plus a verification commit. Accepted on hardware
2026-08-31 after a six-section physical pass and a sixteen-item automated one.

### Audio hardware — VERIFIED, and the pinout is now ground truth

`BSP_AUDIO_VERIFIED` went 0 -> 1. A 1 kHz tone, the MUTE/LOW/MEDIUM/HIGH
sweep, the voice and nine effects were all heard on this board, with MUTE
audibly silent.

    MCLK 16 (a real pin, 256x fs)   BCLK 9   WS/LRCK 45
    DOUT 8 (ESP32 -> codec)         DIN 10 (mic -> ESP32)
    PA enable GPIO 46, plain GPIO, HIGH = on    ES8311 @ 0x18   16000 Hz

Source: waveshareteam/ESP32-S3-Touch-AMOLED-1.8, the **arduino-v2** tree
(`Mylibrary/pin_config.h` and `examples/15_ES8311`). Trusted because the same
file states our already-frozen display and I2C pins byte for byte. Note
`pin_config.h` contains two naming blocks that DISAGREE — `DOPIN`/`DIPIN` are
swapped relative to `I2S_DO_IO`/`I2S_DI_IO`; the working example uses the
`I2S_*` names, and the BSP records this so nobody "fixes" it back. PA is a
plain GPIO, not the TCA9554 bit an older note guessed at.

**One semantic interface.** Gameplay asks for a MEANING (`SND_REFUSE_FOOD`),
never a waveform and never the codec. Rendering runs on its own task and
requests are DROPPED rather than blocking, so audio can never stall LVGL or
the care tick. Verified: a 45-sound burst left the heartbeat unbroken, the
heap flat and the task alive.

**MUTE IS SILENCE AT THE SOURCE** — the digital scale is zero, not codec
attenuation — and it gates nothing visual. A parent may leave this muted
forever and the Visitor must still be able to communicate. The bring-up tone
deliberately bypasses the mute gate so "you are muted" is never mistaken for
"nothing is wired up".

### Settings record, calibration, Gravity Reactions

**Settings live in their own NVS key `visitors/cfg`**, versioned + CRC'd,
20 bytes at v1 and 28 at v2 — NOT in `save_t`. (v2 and its migration arrived
with the v1.0.0 pre-release; see §2f.) Three reasons, worst first: `save_t` is 433 bytes
against a 448 budget and the static_asserts fail at 448; these settings belong
to the DEVICE, so a new Visitor or an `X` must not reset the volume a parent
chose or the calibration a child captured; and the pet blob is rewritten every
few minutes, which is no place for data that changes once in a blue moon. Same
reasoning that put Visit Records aside. A field out of range is treated as
corruption of that FIELD, not the record.

**MOTION is not a second IMU driver.** It reads through
`diag_imu_read_screen()`, the one accessor applying the FROZEN raw->screen
transform, then through the SAME display-frame adapter Tilt Maze established
(`right = +gy`, `down = -gx`) — a swap AND a vertical negation, not a tidy
rotation. Calibration is applied ON TOP as a user offset; the frozen signs are
untouched.

    gravity slide  dead zone 0.15 g (~9 deg); tilt BEYOND it drives the accel.
                   Edges stop AT the edge and dump velocity into a squash.
    upside down    screen +Z points INTO the panel, so z < -0.65 means the
                   panel faces the floor. 1.2 s debounce; relief only if it
                   lasted > 2.5 s.
    shake          a JERK (fast CHANGE in magnitude), not a large reading.
                   Carrying tilts a lot and jerks little.
    annoyance      rises with handling, decays 2/s, FLAVOUR ONLY: no
                   accumulator, no form choice, no evolution, no visit quality.

**GRAVITY OFF gates AMBIENT behaviour ONLY.** Verified again in this pass:
with it off the IMU still reads, calibration still runs, Tilt Maze is
untouched, and no annoyance can accumulate — the event counters sit downstream
of the `blocked` early-return, so that is structural, not incidental.
Default ON.

**CALIBRATION rejects a capture taken while moving** (max spread 0.18 g over
1.4 s / 24 samples). A calibration recorded mid-wave silently tilts every
future reading and would be experienced as a broken Visitor rather than a bad
capture. Verified: a deliberately unsteady capture was refused; a steady one
at an unusual angle was stored, made that angle neutral, and survived a
reboot byte-identical.

### Voice packs, gender and the stage ladder

**Recorded speech replaces the chirp voice for every fixed line.** The chirp
voice went through two generations — pitched tone bursts, then a formant synth
with jitter, glides and consonants. The second genuinely sounds like a mouth,
and it still could not say "I did a mess. It's art.", because formant babble
contains no WORDS. That is inherent, not a tuning failure.

**TWO PACKS, ONE PER GENDER.** Norman (boy) and Kristin (girl), both Piper
models trained on LibriVox recordings and both **public domain** — a
deliberate requirement, since Apple's system voices sound fine but could never
ship. **Both stay mounted** (about 6 KB of PSRAM) and gender picks per line,
so a Visitor hatching the other way needs no reload and no reboot. Verified in
this pass through the PRODUCTION `audio_say()` path, with the Visitor's own
gender printed before and after and unchanged.

Gender remains PRESENTATION ONLY: a voice is presentation, exactly like the
egg colour tinting the Baby, and it reaches no accumulator, form choice, care
rate, discipline roll or evolution path.

**Lines are keyed by a hash of their text** — no ids, no table to keep in
sync. A line with no clip falls back to the chirp voice, so a stale or missing
pack degrades rather than breaks, and the firmware runs fine with no pack
flashed at all.

**Voice settings are hardcoded** so a re-render reproduces the approved voice:
pitch x1.46, pace x1.15, noise 0.70. Pitch and pace are INDEPENDENT — the clip
is synthesised at `length_scale = pace * pitch` and resampled up by `pitch`,
which divides the duration back out, so slowing down costs nothing in
cuteness.

**The stage ladder is only 7% wide** (Baby 1.00 -> Kid 0.98 -> Teen 0.955 ->
Adult 0.93) and that is deliberate: resampling moves pitch and TEMPO together,
so a wide ladder would leave the Adult speaking noticeably slower than the
Baby, and the pace was tuned for intelligibility. Formants stay well above
adult human values at every stage — a small head has a short vocal tract, and
that is what keeps the Adult cute rather than a realistic grown human. Only
the fundamental comes down with age, so it reads as one character growing up.

**REPARTITIONED, because 16 kHz did not fit.** The stock table reserved two
6.5 MB OTA app slots; this device is flashed over USB by hand and has no OTA
path, so the second slot was dead flash. `partitions_visitor.csv` drops it:
filesystem 3.37 MB -> 11.88 MB, app capped at 4 MB so an oversized firmware
fails the BUILD rather than the flash. **NVS stays at 0x9000 size 0x5000,
untouched and load-bearing** — verified across the change by dumping the
Visitor before and after.

294 clips per pack, 16 kHz 4-bit IMA ADPCM, 4.01 + 4.07 MB of 11.88 MB.
**The packs are generated artifacts, NOT in git** (`data/*.bin` is ignored):
~4 MB each, reproducible in ~30 s per voice. See `tools/voicepack/README.md`.
Flash them with `pio run -t uploadfs` (~80 s; does NOT touch NVS).

### Gameplay and game audio

Phase 7 left `games_sfx()` as a stub with every call site already in place, so
all four games came alive by implementing one function. **Memory is the
exception**: it wants a DISTINCT pitch per pad, reproduced when the player
taps, or the sequence is unhearable — so the pads get a major arpeggio and
bypass `games_sfx()`. A wrong tap sounds its OWN note first, then the verdict,
so a wrong note is audible AS a wrong note.

**Rate limits where the naive wiring would buzz:** footsteps are limited in
the walk animation itself (it runs every frame), and the maze wall bump is
limited because a player hugging a wall is legitimately blocked every frame.

The voice is hooked at the point a bubble is ACCEPTED, not where one is
requested, so it can never play for a line that was suppressed, cooled down or
preempted away. **Sound and words are the same event or neither happens.**

### Sleep audio rules

**Night is silent. Only a BABY, only on a DAYTIME nap, at most 2 snores, at a
randomised 45–90 s gap.** A charming snore at 2 pm is a serious problem at
2 am, and this device sits in a child's bedroom overnight.

The gate requires the Visitor to be asleep, the latched period to be a nap,
**the CLOCK to currently say daytime-nap**, the stage to be Baby, budget left,
and the volume not muted. The clock term is load-bearing — see §2e defect 1
below. `care_report()` prints the whole gate, so "is it going to snore
tonight" is answerable in one keystroke instead of by sitting up and
listening.

### Hatch countdown audio

The last `EGG_COUNTDOWN_SEC` (5) seconds chirp once per second, fired on the
second BOUNDARY rather than per refresh — `scr_main_egg_refresh()` runs only
*approximately* once a second, and a countdown that double-beeps or skips is
worse than none. The chime lands as **beat 0 of the hatch**, before the reveal
banner and well before the first words, so the three beats stay strictly
ordered and nothing overlaps.

### Diagnostics added (console, TAB prefix)

The single-key namespace filled up in Phase 9, so Phase 10 sits behind TAB.

    t tone      r audio report   s level sweep   v chirp voice   p effects
    0/1/2/3 volume MUTE/LOW/MED/HIGH
    w stage ladder     k real lines      V pack report    x coverage sweep
    b BOY pack         G GIRL pack
    y gender pack switch, LIVE, through the production audio_say() selector
    f chirp fallback, via a forced lookup miss - no filesystem damage
    h hatch countdown + chime, with no hatch
    q per-game audio sweep
    L speak any line you type
    m motion report    c calibrate      g toggle gravity  n settings
    S arm the motion history recorder
    R restore the clock to 16:00 (awake band, after N/G/A)

Added later by the v1.0.0 pre-release (§2f), same TAB prefix:

    > clock +5 days    < clock -5 days   (CLOCK CORRECTION, not time travel)
    a clock + RTC anchor report
    B backup the Visitor   U restore it   i backup slot info

Everything here is NON-DESTRUCTIVE: the Visitor is never hatched, re-gendered
or ended to test a sound. `audio_set_pack_override()` and
`voice_set_force_miss()` are the two test seams that make that possible, and
both are diagnostic-only — nothing in gameplay may read them.

**The motion history recorder (`TAB S` then `TAB m`) exists for a reason worth
keeping.** Synchronising a serial capture to a human's hands does not work:
two 90 s windows both caught a board sitting flat on a desk, because the flip
happened while the console was not looking. The DEVICE records the extremes
instead — z/right/down ranges, peak tilt and jerk, event counts, blocked
ticks, and the **longest CONTINUOUS hold** past three z levels. That last one
is the measurement that matters: min/max conflates a shake spike (which rails
the accelerometer at its +/-4 g full scale in milliseconds) with a steady
hold, and the trigger only cares about the latter.

### The two defects the verification pass found and fixed

**1. A Baby could snore at night.** `s_sleep_was_nap` is latched on sleep
ENTRY, and entry is SKIPPED when the Visitor is already asleep — so a nap
carrying across into the evening left the flag still reading "nap" at 20:30.
Observed exactly that way: the gate read `was_nap 1` at night, and only an
exhausted snore budget happened to keep it quiet. The gate now ALSO requires
the clock to say daytime-nap, so night is silent by construction rather than
by luck. The latched flag is kept (the wake-up still needs to know which kind
of sleep it was) but it can no longer authorise sound on its own.

Verified before/after: in nap `clock_nap 1 -> SNORES ALLOWED`; jumped to night
still asleep with the budget still at 2, `clock_nap 0 -> silent`, and 110 s
produced nothing. Pre-fix that exact state would have snored within 45–90 s.

**2. The voice coverage sweep was lying about its own coverage.** `total` was
incremented only inside the MISS branch, so every successful lookup went
uncounted and a sweep over a thousand lookups reported "21 checked". It also
sampled 9 of the 18 dialogue selectors — dreams, the hatch greeting,
`food_yum`, `mischief`, `wake`, `stink` and `sleepy_poke` were never asked
about — and it only ever checked the BOY pack.

Now: 179 distinct lines, deduplicated by hash, across BOTH packs (358
lookups), 0 missing. **A verification tool that understates its own coverage
invites exactly the false confidence it exists to prevent.**

While being fixed it raised one FALSE alarm worth recording: it enumerated
`MIS_NONE`, whose `dialogue_mischief()` default is `"Hehe."` — a line
`discipline_misbehave()` early-returns before ever reaching, so it is
correctly absent from the pack. The sweep now enumerates from `MIS_NONE + 1`.

### What the sweep does NOT cover, stated plainly

The dialogue selectors are flavoured by trait AND form, and a trait pool wins
about 70% of draws — so a sweep from a live Visitor can only reach ITS OWN
trait pools plus the generic fallbacks. 179 distinct against a 294-clip pack
is therefore expected, not a fault. The remainder belongs to personalities
that Visitor does not have.

### Not verified, and why

- **Farewell / new Visitor** — necessarily ends the current Visitor, so it was
  not run. Partial coverage: four stored Visit Records show farewell note
  assembly, tone and record-writing all working, and the runtime-assembled
  farewell note is exactly the case the chirp fallback covers.
- **Schema migration fixtures (`V { " #`)** — they overwrite the real save
  with a fake blob and never restore it. Covered structurally instead: Phase
  10 touches neither `storage.h`/`storage.cpp` nor `pet.h` (confirmed by
  `git diff pre-phase10-baseline..HEAD`), and every boot re-confirms
  `sizeof(save_t) 433 / schema 8 / load OK`.
  **This gap is now closable**: `TAB B` / `TAB U` (§2f) back the real save up
  and put it back, so the fixtures can be run on a device carrying a Visitor
  somebody cares about.

### A correction worth keeping

During verification the upside-down reaction was reported as never firing, and
the conclusion drawn was that the detector was aimed at the wrong gesture and
needed changing. **That was wrong.** The recorder later showed a 5216 ms hold
below the threshold firing it correctly; every earlier attempt had simply
never held past vertical for more than 103 ms. The detector is correct and was
NOT changed.

What survives is a weaker TUNING observation, deliberately not acted on: the
only orientation that earns a complaint is the one where the child is looking
at the back of the device and cannot see the bubble, and it needs a deliberate
1.2 s hold that casual play will not produce. That is a design question for
whoever wants it, not a defect.

Related: a reported "pulse down" turned out to be a deliberately odd tilt
calibration (`down +0.764`) making flat read as a 0.77 g tilt, so the slide
was legitimately active. Not a bug.

## 2f. The v1 bug sweep / v1.0.0 pre-release — IN PROGRESS, NOT accepted

Two commits, `c0603c7` and `311f2b9`. Both build and are flashed to the
board. **Neither has been signed off on hardware**, so under the phase gate
neither is finished. §9 lists what decides it.

### c0603c7 — four defects the sweep found

Every one was silent. None crashed, and each produced a Visitor that looked
entirely plausible.

**Per-boundary work ran once, not once per boundary.** `sim_catch_up()` and
`care_tick()` both read `if (pet_apply_stage_for_day(d) > 0) { ...work... }`,
which walks EVERY boundary inside the condition and then does the work for
the final stage only. An absence spanning Baby -> Kid -> Teen never picked a
Kid form: `evo_path[]` was left blank at Kid, and the teen selector's "was a
Good Kid" +/-5 bias read a `form_id` that was still `FORM_BABY` — so a
Visitor cared for well while the device was OFF was structurally denied the
bonus it had earned. `pet_apply_one_stage()` advances at most one boundary
and every caller that does per-boundary work loops on it. Measured with the
new `tools/stagejump` against the same care history crossing the same
boundaries with the device ON: **the teen form differed in 39% of cases and
`evo_path[]` in 100%.**

**Evolution was scored at "now", not at the boundary it was deciding.**
`evolve_scores()` divides by stage days measured against `pet_age_days()`,
which at a boundary reached during a catch-up is the END of the absence. A
Visitor that became a Kid on day 1 but was not switched on again until day 4
had its Baby stage measured as 4.5 days instead of 1.5 — games actually
played divided by time the Visitor had not yet lived, diluting `engage` to a
third. `evolve_scores_on(day)` takes the boundary day; over 8400 care
histories it flips the chosen form in 1% of them outright.

**Every reboot re-simulated the previous session's uptime.** `last_sim_ts`
was written only at boot, so it held the BOOT time for the whole session
while `care_tick()` simulated live from `millis()`. Measured on hardware: a
12-second flash produced a 342-second catch-up, double-charging every meter,
aging floor messes at twice real time, double-weighting the evolution
accumulators and handing `visit_advance()` extra departure evaluations.

**The animation done-callback had two owners and one slot.** `care_init()`
registered the bathroom handler at boot; `evolve_present()` overwrote it at
the first stage change and never restored it. Since every Visitor evolves on
day 1, the bathroom handler was dead for the rest of every visit — no "Oof...
much better.", and, the part that mattered, **no `care_return_to_bed()` after
a bathroom trip taken during sleep hours, so the Visitor was left standing on
the floor at 3 am.** Both handlers already filter by animation and are
naturally disjoint, so `ui_pet_add_done_cb()` registers them side by side.

Also landed: **`storage_backup()` / `storage_restore()`**, a byte-exact copy
of the live save in its OWN NVS namespace `visitorb`. The namespace is the
point — `storage_wipe()` calls `Preferences::clear()`, so a backup stored
beside the save would be destroyed by the very reset it exists to undo. RAW
BYTES, not a `save_t`, so a backup taken before a migration restores what was
actually there rather than a re-serialised interpretation. **This is what
finally makes the destructive fixtures safe to run on a real Visitor** — the
gap §2e had to record as "not verified, and why". Wrap them: `TAB B`, run
`X` or `V { " #`, `TAB U`, then reboot via a full upload or a power cycle.

### 311f2b9 — Date & Time before the hatch, and safe clock correction

The user's report, in their words: a Visitor could hatch while the RTC still
held an old development date, and correcting the date forward several days
afterwards made the Visitor think those days had passed — instant aging and
evolution.

**Why `rtc_trusted()` could not catch it.** It means two things and only two:
the sticky oscillator-stop flag is clear, and the reading is inside a
plausible window. **A development date satisfies both perfectly.** There is
no later moment at which anything can tell.

So the missing fact is recorded explicitly: **`settings.clock_confirmed`** —
"a human set this and the write was read back and verified". It is
DEVICE-scoped rather than part of `save_t`, for the same three reasons the
volume is (§2e): the clock belongs to the device, a new Visitor must not make
a parent re-enter it, and a farewell or an `X` must not wipe it. It is
cleared at boot whenever the RTC has gone untrusted, because at that point it
describes a clock that no longer exists. `scr_main_clock_ready()` is
`rtc_trusted() && settings_clock_confirmed()`, and it is the gate.

**The pre-hatch screen now reads top to bottom as the order things must
happen in:** SET DATE & TIME, colour, gender, START. The date card carries
the instruction and the live clock value on its second line, so "what is it
set to" and "change it" are one control rather than a readout a parent has to
go looking for. START is drawn dead until the clock is confirmed, and a press
on a dead START **opens the setter** rather than doing nothing — a control
that visibly refuses and then offers no route forward is how a parent
concludes the device is broken.

**`hatch_ts` now has exactly ONE production writer**, at the instant the
shell opens, so "a newly hatched Visitor starts at age 0" is true by
construction rather than by a reset something else could undo.
`days_alive_max` is zeroed with it: it is a monotonic floor that
`pet_refresh_age()` clamps `days_alive` UP to, so a predecessor's high-water
mark would otherwise have shown a newborn at its age.

**A CLOCK CORRECTION IS THE OPPOSITE OF A CATCH-UP.** `sim_catch_up()`
answers "the clock moved while we were not looking, so that much life
happened". A parent fixing a wrong date is the other thing entirely: the
clock moved and NOTHING happened. `sim_clock_corrected()` lives beside it in
`sim.cpp` for exactly that reason, and REBASES rather than replaying. Every
timestamp this project stores is an absolute reading of the clock that has
just been found wrong, so they all move by one delta and every DURATION
between them survives:

    hatch_ts        the bug itself - age is (now - hatch_ts), so moving
                    `now` alone ages the Visitor by the correction
    egg_hatch_ts    an absolute deadline; +5d hatches instantly, -5d
                    strands the egg
    depart_due_ts   measures a held farewell against the 48 h cap; forward
                    blows the cap, backward underflows the subtraction
    journal[].ts    dated milestones would print days BEFORE the corrected
                    arrival they followed
    last_play_ts    the repeat-play window (gamerec); either direction
                    wrongly expires a thirty-second-old streak
    last_sim_ts     set to the new reading OUTRIGHT, not shifted

**That last line is the one that kills the fake offline day.** Otherwise the
next boot measures the correction as an absence and charges five days of
hunger, cleanliness, bathroom and departure evaluation that never happened.

**Deliberately NOT rebased**, and this is a decision rather than an omission:
archived Visit Records (sealed history of previous Visitors, no date is ever
rendered, and `days` is a stored duration a correction cannot distort), and
every `millis()` timer (they measure uptime, which an RTC write does not
touch). Anything added later that stores an absolute clock reading has to
join the list in `sim.cpp`, or a correction will silently break whatever
duration it measures.

**The confirm path order is load-bearing.** Read the old value FIRST — after
`rtc_set()` it is gone for good and the delta is unrecoverable. Then write.
Then verify the READ-BACK field by field, because `rtc_set()` proves the
clock is trusted, not that it holds the value asked for. Then rebase. Then
FORCE the save: a power cut between the RTC write and the next periodic save
would bring back a corrected clock beside uncorrected anchors, which is the
original bug reconstructed on the next boot.

**A second defect in the same three lines.** For an EGG, `if (!p->hatch_ts) {
p->hatch_ts = now; pet_apply_stage_for_day(0); }` gave an unhatched Visitor
an age baseline and then — since `STAGE_EGG` is 0, `STAGE_BABY` is 1 and day
0 means Baby — **promoted the egg straight to a Baby**, skipping the
countdown, the colour and gender resolution, the reveal, the first words and
the hatch chime. Setting the clock hatched the egg.

**NO HARDCODED DATES.** The setter seeded from a literal `2026-08-28`, which
is precisely how an RTC ends up plausible and wrong. It now seeds from the
RTC when that is already sensible (so correcting is an adjustment) and
otherwise from `rtc_build_stamp()` — the firmware build date, which is never
in the future and moves every flash. Caveat worth knowing: `__DATE__` is
fixed when `rtc.cpp` is compiled, so an incremental build that does not
recompile it keeps an older stamp. That only makes the seed staler, never
later than now, which is the safe direction. The console clock fixtures lost
their literal dates too — `N`/`G`/`A` move the hands without throwing the
calendar back to whenever the file was written.

**Pre-hatch layout, re-derived.** Fitting a 56 px card into a panel that
already ran to 442 of 448 needed 76 px back. There were only three places to
find them, and the choice is the point:

    the egg      the emotional point of the screen - what the child is
                 waiting for. UNTOUCHED; EGG_ROOT_Y is still -6.
    START        the one control that must be unmissable, and the only one
                 a four-year-old presses alone. Gives up 8 px (72 -> 64)
                 and stays 288 wide.
    the colours  two rows of 80x52 become ONE row of seven 46x56.

    egg preview   root y = EGG_ROOT_Y (-6); the shell spans   30 .. 148
    DATE & TIME  154 .. 210      (320 x 56, two lines of text)
    colour row   230 .. 286      (7 swatches, 46 x 56)
    gender row   306 .. 358      (3 buttons, 108 x 52)
    DEAD SPACE                                                358 -> 378 = 20
    START        378 .. 442      (64 tall, 6 px clear of the 448 panel)

So the colour swatches lost width and gained height; everything else got
bigger or stayed the same. Every gap is `static_assert`ed in `scr_main.cpp`,
and a new assertion **fails the build if any control drops below the 44 px
minimum touch target** — the numbers are checked by the compiler, not by
looking at the panel, which is the standing rule here (§12).

### Diagnostics: correction vs. time travel

**These are opposite tools and confusing them will waste a session.**

    %  .  ,     TIME TRAVEL. Moves hatch_ts BACKWARDS and leaves the clock
                alone, so the Visitor ages through the derived path.
                UNCHANGED by this work, deliberately.
    TAB > <     CLOCK CORRECTION. Moves the WALL CLOCK +/-5 days through the
                production path the Settings page uses. The age must come
                out UNCHANGED; the command prints before/after and PASS/FAIL.
    TAB a       clock + anchor report: health, whether a human confirmed it,
                whether START is allowed, and every anchor a correction
                rebases.

The clock SETTERS (`c`, `N`, `G`, `A`) now route through the same correction
path, so a test of the sleep window is no longer also an untracked test of
the age clock.

### Verified so far, and what that is worth

On the HOST only, and stated as such: unix<->civil round-trip every 6 h
across 2024-2055, age preserved across +/-1, +/-5 and +/-365 day corrections,
the saturation guards, and the space-padded `__DATE__` parser. The layout
gaps are `static_assert`s, so the build passing IS the layout check.

**None of this is a hardware pass.** §9.

### Settings record: v1 -> v2

`visitors/cfg` grew from 20 to 28 bytes for `clock_confirmed` and
`clock_set_ts`. The header had always promised an append was safe; the code
did not keep that promise — the old loader discarded the whole record on a
version bump, which would have cost a parent their volume and a child their
tilt calibration for the sake of one new byte. It now migrates a v1 record
forward, CRC-checked against v1's own extent, with two `static_assert`s
holding the frozen size and the tail offset (the same trap as
`SAVE_V4_SIZE`, §3).

`clock_confirmed` defaults to 0 on that migration, **including on a device
with a trusted clock and a live Visitor.** Inferring "somebody must have set
it" would fabricate the very confirmation the flag exists to demand. The
consequence is mild and intended: an upgrading device asks for the date once,
at its next egg. A live Visitor is untouched — the gate is pre-hatch only.

## 2g. Mischief frequency halved [v1.0.0 pre-release]

Reported on hardware as happening too often. Halved, with ONE config dial:
`MISCHIEF_RATE_PCT` (100 = the Phase 9.5 rate, 50 = half as often).

**It scales the SCHEDULE, not the probability, and that is the whole design.**
The mean interval between opportunities is

    T = mean_gap + cadence x (1 - p) / p

- two terms, because the gap is a hard wait and the roll after it is
geometric. Scaling only one of them changes T by an amount that DEPENDS ON
p, which is exactly the thing that had to stay fixed. Measured against the
real per-roll figures this build produces:

    halve p only     1.00x .. 1.83x    <- 1.00x is a calm Visitor pinned on
                                          the 1% clamp: NO reduction at all,
                                          leaving calm Visitors relatively
                                          WILDER than before
    double gap only  1.07x .. 1.75x
    double cadence   1.25x .. 1.93x
    BOTH             2.00x for every p, exactly

So the dial multiplies the cadence and the gap together and leaves
`mischief_pct()` alone. **Every function in `discipline.cpp` outside
`discipline_report()` is byte-identical** — fair/unfair logic, rewards and
penalties, mischief types, the learned-behaviour EMA and the evolution
history effects were all verified unchanged by diffing the functions, not by
inspection. The only functional change in the build is three derived
constants in `config.h`.

Confirmed on hardware, same Visitor, before and after (`>`):

    Kid    weight 100%   9% per roll   4.5 -> 9.1 min   (6.6/hour)
    Baby   weight  85%   7%            5.3 -> 10.6 min  (5.6/hour)
    Teen   weight  62%   5%            6.8 -> 13.5 min  (4.4/hour)
    Adult  weight  40%   3%           10.1 -> 20.2 min  (3.0/hour)

The percentages are byte-identical before and after, which is the evidence
that the ordering (Kid > Baby > Teen > Adult), the personality modifiers and
the discipline/learned history all still compose as they did. **The mechanic
has not disappeared**: even the calmest stage still offers about three
opportunities an hour.

`>` now prints the FULL interval including the gap, plus the dial's value.
It used to print only "one every N min BEFORE the gap", which is the wrong
number to judge the feel by - at these settings the gap is the larger of the
two terms for a lively Visitor. Turn the dial further only with those
figures in front of you.

A `#error` guard rejects a rate outside 5..400; a zero would have divided by
zero silently in the preprocessor.

## 2h. Four polish items [v1.0.0 pre-release]

### 1. The favourite game is now a real mechanic

`gamerec_favorite()` used to return whichever game had the most plays - a
readout of the PLAYER's habit rather than a trait of the Visitor. It could be
NONE, it never surprised anyone, and two Visitors with the same personality
were guaranteed the same answer. It is now persisted state in the gamerec
record (v1 -> v2, appended, WITH a migration - the old loader discarded the
record on a version bump, which would have cost a child every high score).

**Selection** starts every game at `FAV_W_BASE` 100, adds a row per trait,
and floors at `FAV_W_MIN` 15 so nothing is ever impossible. All numbers are
in config.h. Measured on hardware for a tidy/competitive Visitor, 400 rolls
through the shipped selector:

    weight   HiLo 120   React 150   Memory 190   Maze 80
    expected      22.2%       27.8%        35.2%      14.8%
    observed      24.5%       29.0%        33.8%      12.8%

That Visitor's actual favourite came out Higher/Lower - the 22% option, not
the 35% one - which is the point: personality moves the odds a long way
without ever making the outcome certain.

**Boredom** is in hundredths of a play, so the thresholds read as plays:

    +100  per play of the favourite
    -150  per play of anything else   (MORE than a play adds, so alternating
                                       can never accumulate at all)
     -25  per hour, off the RTC
    switch at a target redrawn per favourite in 400..700, i.e. 4 to 7 plays

Verified: a target of 5.81 plays switched after 6; recovery went 300 -> 150
-> 0 across three other games. The switch **excludes** the current favourite -
0 of 400 rolls picked it again - so boredom always visibly moves somewhere.

**The bonus is happiness only.** It multiplies the same term the repeat
penalty does, so the two compose - observed x1.25 alone, then x0.75 once the
streak penalty joined it. Scores and bests are written from the raw score
inside `gamerec_record_play()`, above and independent of every multiplier, so
neither bonus nor penalty can reach them. Evolution is untouched: `engage`
reads `pet_state.games_played`, which only games.cpp increments.

### 2. The stale Sleepy bug - root cause and fix

**There was no stale flag.** Mood is a pure function of state - Sleepy IS
`energy < ENERGY_SLEEPY_BELOW`, with no latch anywhere - so the Visitor was
not holding anything. It had genuinely not recovered: the nap window is ONE
hour and restores at most 12 energy points (6 with the light on), so a
Visitor that went down below 8 woke up, stretched, said its wake line, and
was still under the threshold of 20. Every piece of code was behaving
correctly and the result was still wrong.

**The fix** guarantees a rested FLOOR when a sleep period that delivered real
rest closes: `slept >= SLEEP_RESTED_MIN_SEC` (20 min) lifts energy to at
least `ENERGY_RESTED_FLOOR` (35), clear of the threshold of 20. A floor,
never a set - a full night is already near 100 and is not dragged down.

It lives in `care_close_sleep_period()`, not in the wake branch, for two
reasons. That is the ONE shared path, so a nap slept in a drawer clears
Sleepy exactly as one slept in front of the child does. And `slept` only
exists there: `care_advance()` closes the period and zeroes
`sleep_accum_sec` BEFORE `care_tick()` reaches its wake branch in the same
tick, so by the time the Visitor visibly wakes, how long it slept is gone.

The wake branch additionally clears `s_sleep_was_nap`, `s_snore_left` and
`s_snore_next`, which used to survive the wake-up - Phase 10 had patched that
leak at the point of USE (the snore gate) rather than at the source.

Regression case, on hardware:

    energy 9   mood Sleepy                      <- precondition
    SLEEP PERIOD: night begins
    energy 15  mood Asleep, accumulated 30 min  <- 15 is STILL below 20
    SLEEP PERIOD: night ends after 30 min
      rested: energy 15 -> 35
    SLEEP: wake up (night); energy 35 -> mood Content
    sleep period: none open  flags 0x00

`care_report()` now prints energy, mood and the sleep period, because it
printed none of them and a test that cannot read what it is testing is not a
test.

### 3. A sleeping Visitor refuses all food

The gate is the FIRST thing in `care_feed()`, ahead of `decide()`. That
placement is the rule, not an optimisation: `decide()` is where "cake is
always accepted" lives, and sleep has to outrank it. Returning there is what
makes the guarantee total - no food object is created, `apply_feed()` never
runs, no walk starts, the bed stays up, the room stays dark, and the sleep
period is never interrupted.

REMOVED from `care_feed()`:

    p->asleep = false;   bed_hide();   scr_main_set_room_dark(false);

i.e. the whole wake-walk-eat-return path. The `care_return_to_bed()` at the
end of the food sequence STAYS, for a different case than it used to serve: a
feed STARTED while awake that runs across the bedtime boundary.

Verified on hardware - burger, fruit and cake, at both ~full and 0% fullness,
during night sleep. All six refused; hunger, weight, cleanliness, happiness,
meals, cakes, mess count and the open sleep period (flags 0x01) were byte
identical before and after. Normal awake feeding is unaffected.

### 4. Battery indicator - MEASURED, not estimated

**How it is physically read on this V2 board.** The AXP2101 PMIC at I2C 0x34
carries a 14-bit VBAT ADC at registers 0x34/0x35. A read-only probe (`TAB P`,
which writes NOTHING) found:

    chip ID    (0x03): 0x4A     <- confirms AXP2101
    ADC enable (0x30): 0x03     <- the VBAT channel is ALREADY ON
    VBAT    (0x34/35): 4058 mV
    gauge      (0xA4): 100%

`0x30 = 0x03` is what makes this feature legitimate here: the VBAT channel is
enabled by the board's own bring-up, so a cell voltage can be read WITHOUT
this project ever writing to the PMIC - which board_pins.h (E) forbids, and
which it still does not do. `BSP_PMIC_VERIFIED` stays 0 and no pin mapping
was touched.

**Voltage to percentage.** VBAT rather than the fuel gauge, because the
gauge's calibration for this pack is unknown (it read 100% at 4058 mV, which
the curve puts at 86%) while a cell voltage means the same thing on every 1S
LiPo. `BATTERY_CURVE` in config.h is 20 anchor points interpolated piecewise -
a LiPo is emphatically not linear, and a straight 3.0-4.2 V line reads 14
points high at 3.8 V. Host-tested: monotonic and in range across the whole
2500-4500 mV window.

Readings outside 2500-4500 mV are rejected as "not a battery". Smoothing is
an EMA on MILLIVOLTS (1/4 weight) rather than on the percentage, because the
curve is steep at the ends; a 2-point deadband on the displayed percentage
then stops a cell on a boundary flickering. **Polling is 30 s**, self
throttled inside `bsp_battery_tick()`, so the 1 s tick can call it freely.

Placement: top-left, x 40..106, y 16..29 - clear of the mood dot (16..28),
the menu handle (316..360), the Visitor (y 150..310), bubbles and the reveal
banner. Nothing in it is clickable. It is hidden entirely until a plausible
reading exists, and hidden while the pre-hatch selectors own that row.

## 3. Save schema

**Schema 8. `sizeof(save_t)` = 433 bytes. `SAVE_SIZE_BUDGET` = 448.**
A `static_assert` in `storage.h` fails the build if the blob overruns, and
two more prove that schema 7 appended exactly 10 bytes to 6 and schema 8
exactly 6 bytes to 7 — leave all three.

Phase 9.5 bumped the schema twice: 7 for the identity and dialogue state,
8 when the ratified dream rules required the sleep period to be persisted.

Migration chain is `v1 -> ... -> v8`, every step "copy the old blob, zero the
tail", because **every schema has appended only**. Frozen sizes:

    SAVE_V1_SIZE 332   SAVE_V4_SIZE 403   SAVE_V6_SIZE 417
    SAVE_V2_SIZE 363   SAVE_V5_SIZE 407   SAVE_V7_SIZE 427
    SAVE_V3_SIZE 397

A wrong frozen size rejects every save of that version as corrupt. This
happened once: `SAVE_V4_SIZE` was briefly set to 407 (which is schema 5) and
would have discarded all v4 saves. Recheck the arithmetic if you add a schema.

**A zeroed tail is not always the right migration.** Schema 7 seeds
`gender_choice` to SURPRISE (the player was never asked), rolls `gender`
once, and starts `learned_mischief` NEUTRAL — zero would mean "every
discipline window this Visitor ever had was corrected", an unearned reward
for a history we do not have. Schema 8's tail genuinely is correctly zero,
and says so explicitly rather than staying silent.

All four hops (v1, v5, v6, v7 -> v8) have console fixtures and have been run
on hardware: `V`, `{`, `#`, `"`.

Separate versioned NVS keys, deliberately NOT part of the pet blob:

- `visitorg/grec` — game records (`gamerec.cpp`)
- `visitorv/recs` — Visit Records, 327 B x 8 = 2624 B (`visitrec.cpp`).
  VISITREC_VERSION is 2; growing the record discarded v1 history once.
- `visitors/cfg` — device settings, `SETTINGS_VERSION` 2, 28 B
  (`settings.cpp`). v1 was 20 B; v2 appends `clock_confirmed` and
  `clock_set_ts` and MIGRATES rather than discarding — see §2f.
- `visitorb/bak` — the safe backup slot (`storage.cpp`), raw bytes of the
  live save. Its own namespace specifically so `storage_wipe()`, which
  clears the whole `visitor` namespace, cannot reach it — see §2f.

Visit Records live apart because the pet blob is rewritten every few minutes
and history must never be at risk from that path.

## 4. The dream rules [PHASE 9.5, ratified]

A dream is a property of a **SLEEP PERIOD**, not of a wake-up event, and the
period is explicit and persisted. Full reasoning in
`docs/PHASE9.5-REQUIREMENTS.md` §2.

1. **A night dream needs at least 2 h of meaningful sleep**
   (`DREAM_MIN_NIGHT_SEC`).
2. **A Baby nap dream needs at least 20 minutes** (`DREAM_MIN_NAP_SEC`).
3. **Baby naps CAN dream once eligible** — then a `DREAM_NAP_CHANCE_PCT`
   (35%) roll, so a nap dream stays a find rather than a fixture.
4. **At most ONE dream per sleep period** (`SLEEPF_DREAMT`).
5. **Repeated reboots during the same night or nap cannot produce a duplicate
   dream** — the period's accumulated duration, its nap-ness and its
   already-dreamt flag are all in the save, so a reboot RESUMES the period
   rather than starting a new one.
6. **Eligibility uses actual recorded sleep duration**, never "is the clock
   currently inside a sleep window". `sleep_accum_sec` accumulates in
   `care_advance()` — the one shared path — so sleep during an absence counts
   identically to sleep in front of the child, and a period spanning both is
   still one period.
7. **All four thresholds are configurable** in `config.h`:
   `DREAM_MIN_NIGHT_SEC`, `DREAM_MIN_NAP_SEC`, `DREAM_NAP_CHANCE_PCT`,
   `DREAM_KEEP`.

Two signals that must not be confused, and were:

- `ctx.asleep` — the Visitor is in bed. Drives the **stat rates**.
- `ctx.sleep_window` — the **clock** says this is a sleep period. Opens and
  closes the period.

`sim_catch_up()` deliberately clears `asleep` at the end of every boot ("you
are here now"), and scripted actions clear it too. Driving the period from it
made a boot at 2 am close the night and open a second one, so one night
produced two dreams.

The dream is only RECORDED when the period closes — which can happen
mid-catch-up with no screen — and `pending_dream` carries it until the live
tick tells it, deferred so it queues behind any return greeting. **There is
no dream hook in `main.cpp` or in the console's absence command.**

Console: `^` runs the eligibility rules against constructed periods; `~`
FORCES a dream and deliberately proves nothing about the rules.

## 5. Gameplay decisions that are load-bearing

**Age is derived, never accumulated.** `pet_age_days()` = `(rtc_now() -
hatch_ts) / 86400.0f`, fractional, 0 when unhatched or the clock is untrusted.
The old code accumulated `elapsed_sec / 86400` in `sim_catch_up()` only, which
(a) never aged a continuously-powered device, (b) truncated every absence under
24 h to zero, and (c) never used `hatch_ts`. `care_tick()` now also calls
`pet_apply_stage_for_day()`. `days_alive` is a cache; `days_alive_max` is
monotonic so a clock correction cannot age backwards.

**A CLOCK CORRECTION IS NOT ELAPSED TIME.** `sim_catch_up()` means "the
clock moved while nobody was looking, so that much life happened". A human
fixing a wrong date means the clock moved and NOTHING happened.
`sim_clock_corrected()` rebases every RTC-anchored timestamp by one delta so
that every duration between them survives, and sets `last_sim_ts` to the new
reading outright so the correction cannot be replayed as an absence on the
next boot. The complete anchor list is in `sim.cpp`; anything added later
that stores an absolute clock reading must join it. Full reasoning in §2f.

**`hatch_ts` has exactly ONE production writer**, at the moment the shell
opens. That is what makes "a newly hatched Visitor starts at age 0" true by
construction. `days_alive_max` is zeroed with it, because it is a monotonic
floor that `pet_refresh_age()` clamps `days_alive` UP to.

**A Visitor may not hatch against an unconfirmed clock.** `rtc_trusted()`
cannot distinguish a correct date from a plausible wrong one, so
`settings.clock_confirmed` records that a human set it and the write was
verified. START is gated on both. §2f.

**Stage boundaries are floats:** Baby 0, Kid 1.0, Teen 3.0, Adult 6.0.
Rule: 1 real day = 1 Visitor year, kept literally — Adult at age 6 is correct
and deliberate; do not fake 13.

**Evolution reads accumulated history, not current meters.** EMAs with a
12-hour half-life (rescaled from 24 h for the shorter lifecycle). Guarantees:
two days perfect from 20 -> 95; one bad day from 100 -> 40, back over 70 in
12 h. Composites CS/BA/IA and ordered first-match selection per stage, with a
±5 teen bias and an improvement-only day-18 recheck.

**Near-boundary evolution is deterministic.** `EVO_EPS` 0.75 neutral zone with
a documented tie-break: **benefit of the doubt**, the kinder branch wins. A
`BA` of -0.035 once routed a Visitor to Mischief Kid on floating-point residue.

**Offline: time is never capped, damage is.** Age, days alive, stage timing and
visit duration always advance the full interval. Hunger, happiness, cleanliness
and unattended accidents are each capped per absence. At most ONE unattended
accident, after which the need parks at 95 (not 100) so the Visitor returns
urgent but with the full grace window.

**One shared advance path.** `care_advance(dt, ctx, budget)` serves the live
tick, fast-forward and every offline chunk. Do not add an offline-specific
implementation — `sim.h` explains why.

**Virtual room light is NOT panel brightness.** `care_set_lights()` toggles a
scene overlay on `scr_main` only. It used to call `bsp_set_brightness()`, which
dimmed the menus too. Physical brightness remains available as a future device
setting. Sleeping Zs render ABOVE the dim layer so they stay bright.

**Sleep overrides tapping.** A poke gets a sleepy grumble; it never wakes the
Visitor or takes it out of bed. Any scripted action finishing while the clock
still says sleep must call `care_return_to_bed()`.

**Bedtime is assertive.** Crossing into the window live gets a walk; being
already inside it (boot at 2 am, or `SLEEP_CATCHUP_MS` 4 s elapsed) snaps
straight into bed.

**Discipline is contextual.** Only real mischief opens a window. Bathroom
accidents from ignored needs, refusing food when genuinely full, hunger,
tiredness and dirt are NEVER misconduct. Unfair presses cost happiness.

**Egg colour is cosmetic only.** It tints the Baby (not later forms, which
carry designed colours that mean something) and must never touch personality,
stats, evolution odds, food preference, discipline or care needs. Random
resolves ONCE at START and is persisted so a power cut cannot reroll it.

**Maze templates are authored offline and BFS-verified**, 4 per stage x 4
transforms = 64, all checked for a route AND a hole-free route. Holes only sit
off the solution route — a 20 px Visitor in a 28 px corridor cannot dodge one.
Never add an on-device generator.

**Farewell tone is the hard rule:** warm, funny, kid-friendly, never
guilt-heavy. A shorter visit is a consequence, not an accusation.

## 6. Pacing pass - COMPLETE, ACCEPTED, tagged

Everything in `PACING-PASS-REQUIREMENTS.md` is implemented and was accepted on
hardware; it is commit `496cf0c`, tag `pacing-balance-baseline`. Highlights
that later work must not undo:

- Variable visit length. `VISIT_LENGTH_DAYS` is gone.
  `VISIT_DEPART_MIN_DAY` 9.0 / `VISIT_DEPART_MAX_DAY` 16.0, mapped
  continuously through a logistic, not a line.
- Stay quality derived from `evolve_scores()` - no parallel counters.
- Departure stability: drift clamp, forward-only notice floor, 36 h lock,
  hint gating.
- Witnessed departure via `SF_FAREWELL_ARM` + `depart_due_ts`.
  **`VISIT_HOLD_MAX_HOURS` (48) is a PRIORITY escalation, not an override.**
  Past it the departure outranks a game, an open menu or a mischief window
  and fires the instant it can - but it NEVER outranks sleep. The farewell is
  witnessed or it does not happen, so the worst case is bounded by the wake
  clock, not by the cap.
- Offline bathroom fairness: the meter PARKS at 95 instead of drifting to 99.

The displayed-age question this section used to leave open was answered by
Phase 9.5: 1 day = 1 Visitor year, said out loud in the child-facing copy.

## 7. Current tuning values

    STAGE_DAY_KID/TEEN/ADULT      1.0 / 3.0 / 6.0   (float)
    ACCUM_HALFLIFE_HOURS          12.0
    EVO_EPS                       0.75

    VISIT_DEPART_MIN_DAY / MAX     9.0 / 16.0
    VISIT_DEPART_MAX_DRIFT         0.15 days per re-evaluation
    VISIT_DEPART_EVAL_HOURS        6.0  simulated hours
    VISIT_DEPART_LOCK_HOURS        36     VISIT_HINT_HOURS 30
    VISIT_DEPART_MIN_NOTICE_H      6.0    VISIT_HOLD_MAX_HOURS 48
    VISIT_STAY_CURVE_K / _X0       8.0 / 0.628   (logistic, not linear)
    VISIT_RECHECK_FRACTION         0.5  of the adult stretch

    SLEEP_START_HOUR / END        20 / 7      NAP 13–14, Baby only
    BATHROOM_SLEEP_RATE           0.25
    BATH_HOURS  Baby 2.5–3.5  Kid 3.0–4.0  Teen 3.5–5.0  Adult 4.0–6.0
    BATH tiers  subtle 60  obvious 80 (45 s)  urgent 95 (20 s)
    BATHROOM_URGENT_PCT           70    BATHROOM_GRACE_MS 90000
    OFFLINE_BATHROOM_PARK_PCT     95    OFFLINE_MAX_ACCIDENTS 1
    SLEEP_CATCHUP_MS              4000  ROOM_DIM_OPA 170

    BURGER  +28 hunger, 0.5 g       partial at 75, refused at 90
    FRUIT   +5  hunger, 0.05 g      refused only at >= 99 (snack, never partial)
    CAKE    +8  hunger, 0.8 g       ALWAYS accepted, counts as junk
    PET_WEIGHT  35–95 g, start 45, +/-20% body width
    GROWTH_SPURT_FRACTION 0.45, baselines 45/52/60/68 g

    OFFLINE caps  hunger 55  happiness 30  cleanliness 40  accidents 1

    --- PHASE 9.5 ---
    EGG_HATCH_SEC 300, 6 palettes + SURPRISE (rainbow shell)
    EGG swatches 80x52, rows y=156/214; gender row 108x52 y=292;
      START y=370 h=72.  Gaps: 26 px above gender, 26 px dead band before
      START, 6 px below.  All static_asserted in scr_main.cpp.
    GENDER_BOY/GIRL/SURPRISE 0/1/2   HATCH_REVEAL_MS 3200  GREET_DELAY 900

    DREAM_MIN_NIGHT_SEC 7200   DREAM_MIN_NAP_SEC 1200
    DREAM_NAP_CHANCE_PCT 35    DREAM_KEEP 3

    --- PHASE 10 ---
    AUDIO   ES8311 @0x18, 16000 Hz, MCLK 16 (256x) BCLK 9 WS 45 DOUT 8 DIN 10
            PA GPIO 46 (plain GPIO, HIGH = on).  VOL_MUTE/LOW/MED/HIGH 0..3
            MUTE = digital zero at the SOURCE, not codec attenuation
    VOICE   2 packs x 294 clips, 16 kHz 4-bit IMA ADPCM, 4.01 + 4.07 MB
            pitch x1.46  pace x1.15  noise 0.70   (baked in at render time)
            stage ladder Baby 1.00 / Kid 0.980 / Teen 0.955 / Adult 0.930
    SNORE_MIN_GAP_MS 45000   SNORE_MAX_GAP_MS 90000   SNORE_MAX_PER_NAP 2
            night ALWAYS silent; Baby + daytime nap + clock agrees only
    EGG_COUNTDOWN_SEC 5  (ticks on the second BOUNDARY, then the chime)

    MOTION_DEADZONE_G 0.15   MOTION_ACCEL 2.20   MOTION_VMAX 7.0 px
    MOTION_UPSIDE_Z -0.65    MOTION_UPSIDE_MS 1200
    MOTION_UPSIDE_RELIEF_MS 2500   MOTION_COMPLAIN_GAP_MS 6000
    MOTION_CAL_MS 1400  _SAMPLES 24  _MAX_SPREAD 0.18 g
    MOTION_ANNOY_PER_FLIP 12.0   decay 2.0/s   (FLAVOUR ONLY)
    SETTINGS  own NVS key visitors/cfg, v1, 20 B, versioned + CRC

    BUBBLE_DEFER_SLOTS 3   _TEXT_MAX 80   _HOLD_MS 120000
    BUBBLE_RECENT_TEXT_MAX 96   (the no-repeat list owns its strings)

    LIGHTS_REACT_GAP_MS 20000   LIGHTS_BACK_CHANCE_PCT 70
    POOP_COMMENT_GAP_MS 300000  POOP_COMMENT_CHANCE_PCT 45

    MISCHIEF_W_BABY/KID/TEEN/ADULT   85 / 100 / 62 / 40
    MISCHIEF_RATE_PCT 50             the ONE frequency dial; 100 = 9.5 rate
    MISCHIEF_CHECK_BASE_MS 15000     -> effective 30000 at 50%
    MISCHIEF_GAP_MIN/MAX_BASE_MS     60000 / 180000 -> 120000 / 360000
    MISCHIEF_BASE_PCT_9_5 14         (unchanged by the dial, deliberately)
    MISCHIEF_SETTLE_HATCH_MS 120000  _BOOT_MS 60000

    FAV_W_BASE 100   FAV_W_MIN 15    per-trait rows in config.h
    FAV_BORE_PER_PLAY 100            FAV_BORE_PER_OTHER 150
    FAV_BORE_DECAY_PER_HOUR 25       target 400..700 (4-7 plays)
    FAV_BONUS_PCT 25                 happiness only; never the score

    ENERGY_SLEEPY_BELOW 20.0   ENERGY_RESTED_FLOOR 35.0
    SLEEP_RESTED_MIN_SEC 1200  (20 min of real sleep earns the floor)

    BATTERY_POLL_MS 30000      BATTERY_MV_MIN/MAX 2500 / 4500
    BATTERY_EMA 1/4 on mV      BATTERY_HYST_PCT 2
    BATTERY_CURVE              20 anchor points, piecewise linear

Spontaneous mischief was HALVED in the v1.0.0 pre-release (§2g). The dial
scales the SCHEDULE only; `mischief_pct()` is byte-identical, so the whole
percentage table and every relative behaviour are untouched. Measured on
hardware, same Visitor before and after:

    Kid    9% per roll   4.5 -> 9.1 min    (6.6/hour)
    Baby   7%            5.3 -> 10.6 min   (5.6/hour)
    Teen   5%            6.8 -> 13.5 min   (4.4/hour)
    Adult  3%           10.1 -> 20.2 min   (3.0/hour)
    LEARN_START 50.0  LEARN_ALPHA 0.22  LEARN_WEIGHT_PCT 60

## 8. Known bugs / open issues

1. **CLOSED 2026-08-30. Evolution reachability at the 12 h half-life is
   VERIFIED: all 12 forms are reachable.** Measured by `tools/evosurvey`,
   which links the SHIPPED `src/evolve.cpp` and drives 24000 achievable care
   regimes x 3 entry priors through the real accumulator, per decision:

       Baby  reachable (start form)      Kids   Good 55092 / Mischief 16908
       Teens Bright 23436 / Mellow 48660 / Rowdy 71904
       Adults Best 3216  Sweet 24720  Playful 10944
              Chonky 101160  Grumpy 27600  Scruffy 48360
       RESULT: 12 of 12 -> PASS

   The counts are how many swept regimes reach each form, i.e. how BROAD the
   route is, not just whether one exists. Best is the narrowest at 3216 and
   is meant to be. Chonky agrees with the figure the user had already
   measured on hardware (IA ~66.8 / CS ~51.7; the survey's example lands
   IA 67.81 / CS 37.02).

   The harness was validated against the device before being trusted, the
   same way the maze sweep was: the live pet's `<` report and the harness fed
   with those exact accumulators agree to 0.002 on CS, exactly on BA and IA,
   and pick the identical form at all three stages.

   Then confirmed END TO END on hardware, which is the part a sweep cannot
   do: reset, hatched, `+` EXCELLENT care re-seeded at the start of each
   stage, aged with `%` through days 1/3/6. Every boundary picked the form
   the harness predicted:

       day 1.00  Baby -> Kid    Good Kid
       day 3.00  Kid  -> Teen   Bright Teen
       day 6.00  Teen -> Adult  Best

   and the Journal read the whole ladder back (`Baby / Good Kid / Bright
   Teen / Best Adult`), so the Phase 9.5 `evo_path[]` fix is holding.

   WORTH KNOWING: extra good care converges hard on Best. The harness gets
   the same lineage from three quite different good-care profiles, including
   one playing only ONE game a day (CS 84.82). There is very little daylight
   between "devoted" and "flawless" under the current thresholds. Not a
   defect; a tuning question, if Best is meant to feel rarer.

   Two things that look wrong in that log and are not:
   - NO growth spurt fires at any boundary under good care. Correct: `+`
     seeds a lean 52 g and the spurt only trims weight ABOVE the new stage
     baseline (Kid 52 / Teen 60 / Adult 68). It takes a cake-fed pet.
   - The finished Adult reads CS 77.84 against Best's effective 77.25.
     `evolve_on_stage_entered()` zeroes the per-stage counters, so `engage`
     drops to 0 immediately after selection. The form is picked at the
     boundary and never re-derived, and the mid-Adult recheck is
     improvement-only, so nothing regresses - but a `<` read straight after a
     transition will always look thinner than the decision actually was.

1b. **NEW, from that survey: the Grumpy branch's sleep clause is DEAD CODE.**
   `evolve_pick_form()` reads

       if (p->care_happy < 40.0f - EVO_EPS ||
           p->care_sleep < 40.0f - EVO_EPS)   return FORM_ADULT_GRUMPY;

   but `care_sleep` is seeded at 50 and `evolve_accumulate()` only ever EMAs
   it toward 45 (lights on) or 95 (lights off). An EMA bounded below by its
   smallest sample cannot cross 39.25. Measured with `tools/evosurvey`
   sleepprobe: 60 days of lights-on sleep asymptotes to exactly 45.0000.

   **STILL OPEN as of Phase 10 acceptance (2026-08-31), and deliberately so.**
   It was explicitly left untouched through the whole Phase 10 verification
   pass: it is a BALANCE change, not a Phase 10 regression, and it belongs in
   the v1 bug/balance sweep where it can be judged against everything else.

   **Grumpy is still reachable** — via `care_happy`, which does reach 3.75
   after 48 h of zero happiness, and the live test pet is a Grumpy Adult that
   got there exactly that way. So no form is unreachable and nothing is
   blocked. What is lost is the DESIGN INTENT that leaving the lights on all
   night can earn a Grumpy Adult on its own; today it cannot contribute at
   all. **Not fixed** — it is a balance change, not a defect, and the phase
   gate says that is the user's call. The one-line options are to raise the
   threshold above 45, or to lower the lights-on sleep sample below it.
2. **The bathroom "next cycle target" log line** now fires and has been
   observed (`BATHROOM: next cycle target 4.55 awake hours (Adult)`), so the
   pacing pass's open question here is CLOSED.
3. **CLOSED 2026-08-30. The snap-to-bed path is VERIFIED on hardware.**
   Clock set to 20:30 with `N` (live crossing took the walk path, as
   expected: `SLEEP: bedtime (night), heading to bed`), then rebooted through
   a full `pio run -t upload` — the only sanctioned reboot on this board.
   The boot log took the other branch:

       SLEEP: bedtime (night) already under way - straight to bed

   `sim_catch_up()` clears `asleep`, so the branch re-runs at boot with
   `s_seen_awake_window` false, which is what makes `already_late` true.
   Post-boot state: mood Asleep, anim `sleeping`, position x=104 y=182 —
   exactly `SLEEP_SPOT_X/Y` (BED_CX 184 - 80, BED_CY 300 - 118), so the
   Visitor was PLACED in bed rather than left walking. No floor sleeping, no
   wandering, no stuck transitional state.
4. **`care_fast_forward()` passes `ctx.offline = false`,** so `T` does not
   exercise the offline accident path. Correct for what `T` means; the
   offline path is exercised by `h` / `H` / `j`.
5. **LVGL heap** sits at ~20-23 KB steady (41-47% of the 48 KB
   `LV_MEM_SIZE`), peaking ~29 KB with the Journal open, fragmentation 1-7%.
   That is ~4 KB above the pre-9.5 baseline: the pre-hatch selector objects
   (seven swatches, six stripes, three gender buttons, the Date & Time card
   with its two labels, the reveal banner) are permanent on `scr_main` and
   stay resident after the Visitor hatches. The v1.0.0 pre-release re-derived
   that layout but did not change the object COUNT materially — one row of
   seven swatches instead of two rows of the same seven, plus three objects
   for the date card. **Re-measure at the next heap report rather than
   assuming.**
   Verified stable across repeated page sweeps - it is a fixed cost, not a
   leak. Do not raise `LV_MEM_SIZE` without measuring a real peak.
6. **A marginal USB cable** was part of the first serial wedge. See §2b.
7. **The AXP2101 charging / external-power bits are NOT interpreted.**
   `TAB P` reads status 0x00 = 0x28 and 0x01 = 0x14 on this board, but which
   bit tracks USB power is not verified HERE, and guessing one would be
   exactly the invented method the brief ruled out. So there is no charging
   icon and the indicator is not hidden on USB. The cell voltage is still
   meaningful either way. `bsp_battery_status_seen()` now accumulates every
   status byte observed since boot, so the test that settles it is: run
   `TAB P`, unplug USB for a minute, plug back in, run `TAB P` again - any
   bit that changed is the one. VBUS and VSYS cannot help: their ADC channels
   read disabled (0x30 = 0x03) and enabling them would be a WRITE.
8. **The fuel-gauge register 0xA4 reads 100% at 4058 mV**, which the curve
   puts at 86%. Deliberately unused - an uncalibrated gauge that says 100%
   most of the way down is worse than a voltage.
9. **The battery indicator has not been looked at on the panel by me** - I
   have no view of the screen. Its coordinates are checked against every
   other element (§2h) and it is not clickable, but a human should confirm it
   reads well before this is called done.
10. **A Baby daytime NAP has not been exercised on hardware** since the fix.
   The test Visitor aged into a Kid mid-session and Kids do not nap. Night
   sleep was verified end to end, and nap and night close through the SAME
   `care_close_sleep_period()` path with only the window and the dream
   threshold differing - but a Baby nap has not been watched.
11. **The v1.0.0 diagnostics inflated this Visitor's play counts.** `TAB Z`
   and `TAB Y` drive the real `gamerec_record_play()`, so plays[] reads
   25/24/36/48 from testing rather than play. Bests are untouched (the
   fixtures pass score 0) and evolution is unaffected (`engage` reads
   `pet_state.games_played`, which only games.cpp increments).

## 9. Exact next steps

1. **VERIFY `311f2b9` ON HARDWARE. This is the blocking item.** Six tests,
   from the user's own report. `TAB a` before and after each gives the
   anchors and a PASS/FAIL age line:

       1  PASSED 2026-09-01. Verified end to end on hardware:
          RTC read Sep 14 2026 (~13.6 days fast) and the settings v1->v2
          migration had left the clock unconfirmed, so this was a REAL
          stale-clock case rather than a staged one.
            START allowed : NO          (gate held, START drawn dead)
            [date corrected on the touch setter]
            confirmed     : YES, by a human, verified
            START allowed : yes
            egg_hatch_ts  : 1819810748  (countdown running)
          and at the hatch:
            hatch_ts      : 1819810748  <- the exact countdown deadline,
                                           stamped off the corrected clock
            pet_age_days  : 0.0000 at the hatch
            days_alive 0, days_alive_max 0, stage Baby
       2  live Visitor -> clock +5 days  -> age preserved   (TAB >)
       3  live Visitor -> clock -5 days  -> age preserved   (TAB <)
       4  neither correction produces fake offline simulation:
          no aging, no evolution, no hunger/cleanliness/bathroom advance,
          no departure trigger, no journal entries
       5  reboot -> corrected RTC and preserved age/state both survive
       6  a GENUINE powered-off absence afterwards still ages and
          simulates correctly (`h` / `H` / `j`, or a real power-off)

   Worth exercising alongside: hatch with the clock unconfirmed and check
   START is dead and the setter opens; correct the clock MID-COUNTDOWN and
   check the countdown neither fires early nor strands.

2. **Verify `c0603c7` on hardware.** Largely covered by normal play plus the
   `%` age jumps across two boundaries in one step, which is the case the
   per-boundary fix exists for. `TAB B` / `TAB U` now make the destructive
   migration fixtures safe to run (§8, "Not verified, and why").

3. Only after both: tag. **Do NOT tag `v1.0.0` and do NOT merge to `main`
   without being asked.** Delete `safety/v1.0.0-pre-release-2026-09-01` once
   the sweep is tagged (§1).

4. Still awaiting a decision on the dead Grumpy sleep clause (§8.1b). It is a
   BALANCE change, deliberately not made unilaterally. It was left untouched
   through Phase 10 verification AND through this sweep so far — it is still
   the user's call.

5. **Verify §2h on hardware where I could not.** Specifically: look at the
   battery indicator on the panel (§8.9), and watch a BABY take a daytime nap
   and clear Sleepy (§8.10). Everything else in §2h was verified on hardware
   and the traces are in that section.
6. Optionally settle the AXP2101 external-power bit with the unplug test in
   §8.7 - that is what a charging indicator would need.
7. The sweep is not finished as a sweep. What has been swept so far is the
   clock/age/hatch surface, the four defects in §2f, and the four polish
   items in §2h; nothing has systematically gone after the menu/pager, the
   bubble system or the farewell path.

Voice packs are NOT in git. A fresh clone needs `pio run -t uploadfs` with
`data/voice_boy.bin` and `data/voice_girl.bin` rebuilt per
`tools/voicepack/README.md`, or the Visitor falls back to chirps — which is a
supported degraded mode, not a failure.

## 10. Build / flash / test

    cd /Users/daniel/Projects/tamagotghi
    pio run                                        # build
    pio run -t upload --upload-port /dev/cu.usbmodem101

Board is at `/dev/cu.usbmodem101` (ESP32-S3 native USB-Serial/JTAG, MAC
28:84:85:8d:51:68). The port survives reset, so a monitor can be held open
across reboots.

**Always release the port when finished** — the user runs their own
`pio device monitor -p /dev/cu.usbmodem101 -b 115200` and a stale holder blocks
esptool with `Errno 35`. Kill any holder before flashing:

    H=$(lsof -t /dev/cu.usbmodem101); [ -n "$H" ] && kill $H

If the board stops responding (`No serial data received`, no serial output):
unplug the USB for ~5 s, or hold BOOT + tap RESET to force the ROM bootloader.
Do not retry esptool in a loop.

Helper scripts used for automated testing live in the session scratchpad and
are NOT in the repo — recreate as needed. They are ~15 lines of pyserial:
open the port, optionally toggle RTS to reset, write keystrokes, read for N
seconds. Use `~/.platformio/penv/bin/python` (it has pyserial).

### Console keys (serial, one letter, no Enter)

    ?  help              *  age-clock report      <  evolution explain
    .  age +6h           ,  age +1h              >  discipline report
       ^ these move hatch_ts (TIME TRAVEL). To move the WALL CLOCK, see the
         TAB keys below - they are opposite tools and mixing them up will
         cost you a session.
    +  EXCELLENT care    =  MID care             _  POOR care
    F  force next stage  O  arm offline reveal   /  force mischief window
    h/H/j  simulate 8h / 72h / 8 days away       T  fast-forward 30 min
    N/G/A  clock -> bedtime / wake / nap         l  toggle lights
           (these keep TODAY'S date and rebase the Visitor's anchors)
    c  set the RTC to the firmware build stamp (never a hardcoded date)
    7/8/9  burger / fruit / cake   0  bathroom   C  clean
    Q/q/E/z  the four games        K  exit game  a  game records
    J  visit records     @  jump to departure  ;  acknowledge  :  start egg
    $  departure report + calibration table   !  age pending departure 24h
    &  GOOD care seed    %  age +24h
    V  v1->v8   {  v5->v8   #  v6->v8   "  v7->v8   (migration tests)
    --- Phase 9.5 ---
    U  dialogue samples + About Me     I  identity / growth / behaviour
    ~  FORCE a dream (bypasses rules)  ^  dream ELIGIBILITY rules
    (  deferred-reaction test          )  old-mess trigger probe
    }  learned-behaviour recovery demo
    :  START the egg (90 s)   |  cycle colour   -  cycle gender
    `  maze collision sweep (all 16 mazes)
    \  press the open game's Start    '  place the Visitor on the maze exit
    X  reset Visitor     Y  persistence fidelity  y  suspend simulation
    m  LVGL heap         v  pager/pet state       s  storage self-test
    M  menu toggle       P/D  pet screen / Phase 1 test card
    --- v1.0.0 pre-release (TAB prefix) ---
    TAB >  clock +5 days     TAB <  clock -5 days   (CLOCK CORRECTION:
           drives the production path; the age must NOT change, and the
           command prints before/after and PASS/FAIL)
    TAB a  clock + RTC anchor report (health, confirmed?, START allowed?,
           every anchor a correction rebases)
    TAB B  backup the Visitor   TAB U  restore it   TAB i  slot info
           (own NVS namespace; survives X and the V { " # fixtures)
    TAB P  AXP2101 READ-ONLY probe (writes NOTHING) + battery state
    TAB F  favourite-game weighting: 400 rolls, non-destructive
    TAB Z  boredom walk    TAB Y  boredom recovery
           (both DO advance this Visitor's play counts - TAB B first)

`y` leaves simulation SUSPENDED and is not persisted — power-cycle to clear.
A common self-inflicted test failure is leaving it on and concluding a feature
is broken.

## 11. Phase 10 scope — DELIVERED

Kept for the record; see §2e for what was actually built and verified.

- IMU personality interactions (shake, tilt, upside down) via the frozen axis
  mapping and the display-frame adapter — DONE.
- User-facing tilt calibration — DONE, with a reject-while-moving guard.
- Audio subsystem and a four-way volume control — DONE. Mute silences sound
  ONLY; all bubbles and visual feedback continue.
- Night-time sleep quiet, Baby daytime nap snores only, never repetitive
  all-night snoring — DONE, and hardened during verification (§2e defect 1).
- The three reserved Settings cards (Volume, Recalibrate Tilt, Gravity
  Reactions) are live and show their current value in the label, so "is it
  muted?" is answered by looking.
- `BSP_AUDIO_VERIFIED` is now 1 and the pinout is ground truth (§2e).
- Settings did NOT go into `save_t`; they have their own NVS key, so the
  15 bytes of save headroom noted here previously are still free.

## 12. Working style the user expects

- Strict phase gates. Finish, flash, verify on hardware, report, stop.
  Do not start the next phase without being asked.
- Report honestly. Partial delivery clearly labelled beats a claimed finish.
  When a test is invalidated (usually by leaving simulation suspended, or by
  the user interacting mid-test), say so rather than reporting the result.
- Verify before asserting. Several real bugs here were found by checking
  arithmetic rather than trusting a plausible explanation: the egg-selector
  hitbox overlap (-4 px), the `SAVE_V4_SIZE` error, the invisible hatched Baby,
  the untracked Higher/Lower timer, and the destructive storage self-test.
  The v1 sweep added more of the same kind: the clock that hatched the egg,
  and the per-boundary work that ran once instead of once per boundary —
  both found by reading the control flow, not by reproducing a symptom.
- Photos from the user are the fastest way to diagnose rendering problems.
- Existing docs worth reading: `PHASE1-RESULTS.md`, `PHASE2-RESULTS.md`,
  `PHASE4-REQUIREMENTS.md`, `PHASE5-6-OFFLINE-REQUIREMENTS.md`,
  `PHASE9-JOURNAL-REQUIREMENTS.md`, `PHASE10-AUDIO-REQUIREMENTS.md`,
  `PACING-PASS-REQUIREMENTS.md`.
