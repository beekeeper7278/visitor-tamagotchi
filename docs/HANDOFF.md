# Visitor — Continuation / Handoff

Written 2026-08-29 at the end of the gameplay pacing & balance pass, before a
context compaction. Everything here was verified against the tree or the
device at the time of writing; where something is unverified it says so.

---

## 0. READ THIS FIRST — the working tree is ahead of the last tag

**All of Phase 8, all of Phase 9 and the entire pacing pass are UNCOMMITTED.**

The newest tag is `phase7-games-baseline` (`c39b3e1`). Everything since —
evolution, personality, discipline, journal, Visit Records, farewell, the egg,
the age-clock rework, schema 4 and 5 — exists only as modified/untracked files
in the working tree.

Untracked files that are entire subsystems:

    src/evolve.{h,cpp}      src/discipline.{h,cpp}
    src/journal.{h,cpp}     src/visitrec.{h,cpp}
    src/farewell.{h,cpp}    docs/PACING-PASS-REQUIREMENTS.md

**Do not run `git checkout .`, `git reset --hard`, `git stash` or `git clean`.**
Any of them destroys several phases of accepted, hardware-verified work.

The user runs a strict phase gate: work is committed and tagged only after they
accept it on hardware. Phase 8+9 was never formally accepted and the pacing
pass is incomplete, which is why it is all still uncommitted. Ask before
committing it; do not commit unaccepted work to `main` unilaterally.

---

## 1. Current status

| Phase | State |
|---|---|
| 1 Hardware baseline | COMPLETE, tagged |
| 2 Pet renderer / animation / bubbles | COMPLETE, tagged |
| 3+4 Menu, pages, food, care, bathroom, messes | COMPLETE, tagged |
| 5+6 RTC, aging, sleep, persistence, offline catch-up | COMPLETE, tagged |
| 7 All four games | COMPLETE, tagged |
| 8 Evolution / personality / discipline | COMPLETE, **uncommitted** |
| 9 Journal / Visit Records / farewell / egg | COMPLETE, **uncommitted** |
| Pacing & balance pass | **PARTIAL** — see §6 |
| 10 IMU personality, tilt calibration, audio | NOT STARTED |

**Do not begin Phase 10** until the pacing pass is finished and accepted.

## 2. Git

    c39b3e1  (tag: phase7-games-baseline)  Phase 7 baseline: all four games
    c59a2fb  (tag: phase5-6-baseline)      RTC, aging, sleep, persistence, offline
    ba8705b                                Record Phase 5+6 offline requirements
    7bb370b  (tag: phase3-4-baseline)      Menu, six pages, food, care, messes
    2828dd2                                Record Journal and audio requirements
    e751e42                                Record Phase 4 design requirements
    14cb83a  (tag: phase2-pet-baseline)    Pet sprite, animation, speech bubbles
    17f0727  (tag: phase1-hardware-baseline)

Branch `main`. Nothing has ever been pushed — the user has said "do not push"
each time. There is no remote configured for this work.

## 3. Save schema

**Schema 5. `sizeof(save_t)` = 407 bytes. `SAVE_SIZE_BUDGET` = 448.**
A `static_assert` in `storage.h` fails the build if the blob overruns — leave it.

Migration chain is `v1 -> v2 -> v3 -> v4 -> v5`, every step "copy the old blob,
zero the tail", because **every schema has appended only**. Frozen sizes:

    SAVE_V1_SIZE 332   SAVE_V3_SIZE 397
    SAVE_V2_SIZE 363   SAVE_V4_SIZE 403

A wrong frozen size rejects every save of that version as corrupt. This
happened once: `SAVE_V4_SIZE` was briefly set to 407 (which is schema 5) and
would have discarded all v4 saves. Recheck arithmetic if you add a schema.

Separate versioned NVS keys, deliberately NOT part of the pet blob:

- `visitorg/grec` — game records (`gamerec.cpp`)
- `visitorv/recs` — Visit Records, 233 B x 8 = 1872 B (`visitrec.cpp`)

Visit Records live apart because the pet blob is rewritten every few minutes
and history must never be at risk from that path.

## 4. FROZEN — hardware, do not touch

`include/board_pins.h` carries a FROZEN banner. **Do not change** the V2 pin
map (QSPI D0–D3 = 4/5/6/7, SCLK 11, CS 12, I2C SDA 15 / SCL 14), the 40 MHz
QSPI clock, `BSP_LCD_RST = -1`, the 368x448 geometry, the column offset 16, the
TCA9554 reset sequence, the touch transform, or the IMU axis mapping — unless a
**reproducible hardware failure** demands it, and then re-run the Phase 1
diagnostic and update `docs/PHASE1-RESULTS.md` in the same commit.

Related rules that are easy to break by accident:

- **Never full-erase NVS.** Use plain `pio run -t upload`. A factory erase
  destroys the pet. Normal upload only erases the sectors it writes; NVS at
  `0x9000–0xdfff` is untouched.
- **The tilt maze uses a display-frame adapter over the frozen IMU mapping**
  (`games.cpp`, `mz_step`): `tilt_right = gy`, `tilt_down = -gx`. This is a
  swap AND a vertical negation, determined on hardware. The tidy "pure 90°
  rotation" theory was wrong. Do not "simplify" it and do not edit the frozen
  signs in `board_pins.h` instead.
- `diag_storage_report()` must stay non-destructive. It used to write test
  values into the one real save slot on every boot, destroying the pet. It now
  snapshots and restores. Do not undo that.

## 5. Gameplay decisions that are load-bearing

**Age is derived, never accumulated.** `pet_age_days()` = `(rtc_now() -
hatch_ts) / 86400.0f`, fractional, 0 when unhatched or the clock is untrusted.
The old code accumulated `elapsed_sec / 86400` in `sim_catch_up()` only, which
(a) never aged a continuously-powered device, (b) truncated every absence under
24 h to zero, and (c) never used `hatch_ts`. `care_tick()` now also calls
`pet_apply_stage_for_day()`. `days_alive` is a cache; `days_alive_max` is
monotonic so a clock correction cannot age backwards.

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

## 6. Pacing pass — what is NOT done

Implemented: age clock (§0), stage boundaries (§1), half-life rescale and
`stage_days` floor (§3), schema 5 + migration (§5), per-stage randomised
bathroom targets and the 0.25 sleep multiplier (§11/§12), warning tiers (§14),
egg-selector layout fix (§15), time-travel console commands.

**Not implemented:**

- §6 variable visit length. `VISIT_LENGTH_DAYS` is STILL 21 in `config.h` and
  still used by `farewell_due()` and the `@` command. The 9–16 day envelope,
  `VISIT_DEPART_*` constants and the continuous mapping do not exist.
- §7 `stay_quality` — no rolling/decayed stay score. Must be derived from
  `evolve_scores()`, NOT a parallel counter set.
- §8 departure stability — no drift clamp, no forward-only rule, no 36 h lock,
  no hint gating.
- §9 witnessed departure — `farewell_due()` still fires from `sim_timer_cb`
  regardless of sleep or absence. `SF_FAREWELL_ARM` exists in `save_t.flags`
  and is the intended flag; it is unused.
- §10 farewell/Visit Record fields for stay length and care band.
- §13 offline bathroom fairness on return.
- §2 simulation-advance harness. Only the age-clock shift exists; `.` and `,`
  move `hatch_ts` and do NOT exercise hunger/sleep/bathroom/care history.
- §16 GOOD care seed (`diag_seed_care(3)`) — only POOR/MID/EXCELLENT exist, so
  three fixtures for four target bands.
- Most of §17's matrix, the departure-day calibration table, the age-copy
  audit (§4), and Chonky reachability after the half-life change.

## 7. Current tuning values

    STAGE_DAY_KID/TEEN/ADULT      1.0 / 3.0 / 6.0   (float)
    ACCUM_HALFLIFE_HOURS          12.0
    EVO_EPS                       0.75
    VISIT_LENGTH_DAYS             21   <- still fixed; §6 replaces this

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
    EGG_HATCH_SEC 300, 6 palettes + Random
    EGG swatches 80x60, rows y=180/250, START y=350 (40 px dead space)

## 8. Known bugs / open issues

1. **Everything after Phase 7 is uncommitted.** Highest risk item here.
2. **`VISIT_LENGTH_DAYS` 21 is inconsistent with the new lifecycle.** Adult
   starts at day 6 and the visit is meant to end between 9 and 16.
3. **Evolution is currently being judged on rescaled accumulators that have
   not been re-tested.** All 12 forms were reachable at the 24 h half-life;
   reachability at 12 h — especially Chonky — is UNVERIFIED.
4. **Age-related copy has not been audited.** Stats, Pet Info, Journal,
   milestones, farewell, Visit Records may reference the old 3/7/13 pacing.
5. **The bathroom "next cycle target" log line was not observed** during the
   last test window. The rate behaved correctly (26%/h -> ~2.7 h target,
   inside the Baby band) but confirm `care_new_bath_target()` actually fires.
6. **The snap-to-bed path is unverified on hardware.** The walk path is
   confirmed. To test: set the clock to 20:30 with `N`, then power-cycle; the
   log should say "already under way - straight to bed".
7. **`care_fast_forward()` passes `ctx.offline = false`,** so the `T` command
   does not exercise the offline accident path.
8. **LVGL heap** sits at 15–17 KB steady, 32–35% of the 48 KB `LV_MEM_SIZE`,
   fragmentation 1–2%. Phase 7's Games page peaked at 59%. No leak found in any
   subsystem. Do not raise `LV_MEM_SIZE` without measuring a real peak.

## 9. Exact next steps

1. Ask the user whether to commit the Phase 8/9/pacing work, then do it.
2. Finish the pacing pass in the order the spec gives: simulation-advance
   harness and the GOOD seed first (nothing else is testable without them),
   then stay quality from `evolve_scores()`, then departure stability, then
   witnessed departure, then farewell/Visit Record fields, then §13.
3. Re-verify evolution reachability at the 12 h half-life, Chonky especially.
4. Audit age-related copy (§4) and report; do not rewrite copy unilaterally —
   the user decides.
5. Produce the departure calibration table for all four seeds.
6. Only then, Phase 10.

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
    +  EXCELLENT care    =  MID care             _  POOR care
    F  force next stage  O  arm offline reveal   /  force mischief window
    h/H/j  simulate 8h / 72h / 8 days away       T  fast-forward 30 min
    N/G/A  clock -> bedtime / wake / nap         l  toggle lights
    7/8/9  burger / fruit / cake   0  bathroom   C  clean
    Q/q/E/z  the four games        K  exit game  a  game records
    J  visit records     @  farewell   ;  acknowledge   :  start egg (10 s)
    X  reset Visitor     Y  persistence fidelity  y  suspend simulation
    m  LVGL heap         v  pager/pet state       s  storage self-test
    M  menu toggle       P/D  pet screen / Phase 1 test card

`y` leaves simulation SUSPENDED and is not persisted — power-cycle to clear.
A common self-inflicted test failure is leaving it on and concluding a feature
is broken.

## 11. Phase 10 scope (DO NOT START YET)

- IMU personality interactions — reactions to shaking, tilting, being upside
  down, using the frozen axis mapping via the display-frame adapter.
- User-facing tilt calibration.
- Audio subsystem and a four-way volume control (Mute / Low / Medium / High).
  Mute silences sound ONLY; all bubbles and visual feedback continue.
  Night-time sleep is quiet; a Baby may make one or two daytime nap snores;
  never repetitive all-night snoring — this device sleeps in a child's bedroom.
  Requirements are recorded in `docs/PHASE10-AUDIO-REQUIREMENTS.md`.
  **Audio is hardware-blocked:** `BSP_AUDIO_VERIFIED` is 0. The ES8311 answers
  at 0x18 but the I2S routing, MCLK and PA enable line are all unverified.
  A UI hook for volume is reserved on the Pet Info page.

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
- Photos from the user are the fastest way to diagnose rendering problems.
- Existing docs worth reading: `PHASE1-RESULTS.md`, `PHASE2-RESULTS.md`,
  `PHASE4-REQUIREMENTS.md`, `PHASE5-6-OFFLINE-REQUIREMENTS.md`,
  `PHASE9-JOURNAL-REQUIREMENTS.md`, `PHASE10-AUDIO-REQUIREMENTS.md`,
  `PACING-PASS-REQUIREMENTS.md`.
