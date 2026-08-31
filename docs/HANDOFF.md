# Visitor — Continuation / Handoff

Rewritten 2026-08-30 at the end of Phase 9.5.

---

## 0. Status

**Phase 9.5 (personality, dreams, identity, refinement) is COMPLETE and
ACCEPTED on hardware.** The tag `phase9.5-polish-baseline` covers it together
with the Tilt Maze regression fix that followed acceptance (see §2c).

**The two pre-Phase-10 checks are DONE and both PASSED** (evolution
reachability at the 12 h half-life, and the snap-to-bed path). See §8.

**Phase 10 has NOT been started.** It was briefly authorised on 2026-08-30 and
the authorisation was withdrawn in the same session, before any Phase 10 work
began — no audio discovery, no IMU work, no code. Treat it as not started.

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
| **9.5 Personality / dreams / identity / refinement** | **COMPLETE, tagged `phase9.5-polish-baseline`** |
| 10 IMU personality, tilt calibration, audio | NOT STARTED |

Full detail for 9.5 is in `docs/PHASE9.5-REQUIREMENTS.md`, including the
eight defects the verification pass found and fixed.

**Do not begin Phase 10 without being asked.**

## 1. Git

Branch `wip/phase8-9-pacing`. Nothing has ever been pushed — the user has
said "do not push" each time, and there is no remote configured.

    HEAD    (tag: phase9.5-polish-baseline)  Phase 9.5: personality, dreams,
                                             identity, refinement
    496cf0c (tag: pacing-balance-baseline)   Pacing & balance pass
    acdd3a1  WIP: Phase 8-9 and gameplay pacing pass
    c39b3e1 (tag: phase7-games-baseline)
    c59a2fb (tag: phase5-6-baseline)
    7bb370b (tag: phase3-4-baseline)
    14cb83a (tag: phase2-pet-baseline)
    17f0727 (tag: phase1-hardware-baseline)

`main` is still at `e8726d0`. The phase gate stands: work is committed and
tagged only after the user accepts it on hardware. Ask before committing; do
not merge to `main` unilaterally.

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

`EGG_ROOT_Y` (-6) exists because the SELECTOR screen needs the room: two
colour rows, a gender row and START fill everything below 156. The moment
START is pressed all of that disappears, and the shell was left stranded
under the HUD with three quarters of the panel empty below it.

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

    BUBBLE_DEFER_SLOTS 3   _TEXT_MAX 80   _HOLD_MS 120000
    BUBBLE_RECENT_TEXT_MAX 96   (the no-repeat list owns its strings)

    LIGHTS_REACT_GAP_MS 20000   LIGHTS_BACK_CHANCE_PCT 70
    POOP_COMMENT_GAP_MS 300000  POOP_COMMENT_CHANCE_PCT 45

    MISCHIEF_W_BABY/KID/TEEN/ADULT   85 / 100 / 62 / 40
    MISCHIEF_CHECK_FAST_MS 15000     MISCHIEF_BASE_PCT_9_5 14
    MISCHIEF_GAP_MIN/MAX_MS          60000 / 180000
    MISCHIEF_SETTLE_HATCH_MS 120000  _BOOT_MS 60000
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
   (seven swatches, six stripes, three gender buttons, the reveal banner) are
   permanent on `scr_main` and stay resident after the Visitor hatches.
   Verified stable across repeated page sweeps - it is a fixed cost, not a
   leak. Do not raise `LV_MEM_SIZE` without measuring a real peak.
6. **A marginal USB cable** was part of the first serial wedge. See §2b.

## 9. Exact next steps

1. Nothing is outstanding for Phase 9.5. It is committed and tagged.
2. DONE — evolution reachability verified, 12 of 12 forms (§8.1).
3. DONE — snap-to-bed verified on hardware (§8.3).
4. Awaiting a decision on the dead Grumpy sleep clause (§8.1b). Balance
   change, deliberately not made unilaterally.
5. Phase 10 only when asked. Nothing about it has been started.

Also landed alongside the two checks: the `tools/evosurvey` harness, a
one-line fix in `evolve_explain()` (its accumulator header printed the literal
"24h half-life" for the whole of the 12 h era, in the one diagnostic used to
reason about the accumulators - it now prints ACCUM_HALFLIFE_HOURS), and the
hatch-countdown layout of §2d.

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
  UI hooks for Volume, Recalibrate Tilt and Gravity Reactions are reserved
  on the SETTINGS page (renamed from Pet Info in Phase 9.5) as visible,
  disabled cards. Phase 10 wires them; 15 bytes of save headroom remain.

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
