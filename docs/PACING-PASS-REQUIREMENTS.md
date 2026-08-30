# Visitor — Gameplay Pacing & Balance Pass

**Project state:** Phases 1–9 complete. This pass sits between Phase 9 and Phase 10 (Tilt Maze).

**Do not start Phase 10 until every change below is implemented, compiled, flashed and verified on the real board.** Finish with the report in §14 and stop.

The lifecycle is too slow for how Visitor actually gets played: short sessions, a few times a day, by kids. This pass compresses the arc and makes the ending earned rather than scheduled.

Everything below names real symbols in this repo. Where I cite a file and line, check it before you change it — I read the tree at schema 4 / `VISITOR_VERSION "0.3.0-phase3+4"` and a line may have moved.

---

## 0. BLOCKING PREREQUISITE — the age clock is broken

**Do this first. Nothing else in this document works until it is fixed.**

Age is currently advanced in exactly one place, `sim.cpp`:

```c
p->days_alive = (uint16_t)(p->days_alive + s_rep.elapsed_sec / 86400UL);
```

Three consequences, all of which I have traced through the code:

1. **`care_tick()` never ages the Visitor.** It calls `care_advance()` and handles sleep presentation, but never touches `days_alive` and never calls `pet_apply_stage_for_day()`. A device left switched on does not get older and never changes stage.
2. **Integer truncation loses time.** `elapsed_sec / 86400` returns 0 for any absence under 24 hours. Twelve two-hour gaps advance the age by zero days. Real elapsed time is silently discarded on every short absence — which is the normal usage pattern.
3. **`hatch_ts` is stored but never used to derive age.** `pet_state_t.hatch_ts` exists, is persisted (`save_t.hatch_ts`), and is only ever tested for non-zero.

Under the current day 3 / 7 / 13 boundaries this drifts slowly enough to look like a tuning problem. Under the new day 1 / 3 / 6 boundaries the same drift spans whole stages.

**Required fix:** derive age from the clock, not from accumulation.

- Add `float pet_age_days(void)` computing `(rtc_now() - hatch_ts) / 86400.0f`, returning 0 when `hatch_ts == 0` or the RTC is untrusted.
- `days_alive` becomes a derived cache refreshed from that, not an accumulator. Keep it `uint16_t` in `save_t` for schema compatibility, but stop adding to it.
- Call `pet_apply_stage_for_day()` from `care_tick()` as well as from `sim_catch_up()`, so a continuously-powered device evolves on time.
- Keep `save_t.days_alive_max` monotonic as it is now, so a clock correction can never age the Visitor backwards.
- **Stage boundaries must compare against fractional days.** `stage_for_day()` takes `uint16_t`; with a 1-day Baby stage, whole-day granularity means a Visitor hatched at 18:00 becomes a Kid at midnight rather than 24 hours later. Move the comparison to `float` days.

State in your report what `pet_age_days()` returns for a Visitor hatched 90 minutes ago, and confirm a continuously-powered device now crosses a boundary without a reboot.

---

## 1. New life-stage timing

Keep the core rule: **1 real day = 1 Visitor year.**

Replace the boundaries in `config.h`:

```c
/* was: 3 / 7 / 13 */
#define STAGE_DAY_KID           1.0f
#define STAGE_DAY_TEEN          3.0f
#define STAGE_DAY_ADULT         6.0f
```

| Stage | Days | Length |
|---|---|---|
| Baby | 0.0 – 1.0 | 1 day |
| Kid | 1.0 – 3.0 | 2 days |
| Teen | 3.0 – 6.0 | 3 days |
| Adult | 6.0 – departure | 3–10 days |

`STAGE_EGG` and the `EGG_HATCH_SEC` 5-minute hatch are unchanged; day 0 is measured from `hatch_ts`.

Everything reading these must follow: `pet.cpp: stage_for_day()` and `pet_apply_stage_for_day()` (including the `boundary` lookup that writes `stage_day[]`), `sim.cpp` catch-up, `evolve.cpp`, journal dates, the `diag.cpp` console, and the Pet Info page.

### 1a. Displayed age will change, and you must audit it

With 1 day = 1 year kept literally, Visitor becomes an Adult at **age 6** instead of age 13. Audit every place an age is shown or written — `pages.cpp` Stats and Pet Info, `journal.cpp` line rendering, `farewell.cpp` note composition, `visitrec.cpp` — and report anything that now reads wrong. Do not rewrite copy unilaterally; list it and I will decide.

### 1b. Three side effects of shorter stages

These are not optional; each one is load-bearing and each one breaks quietly.

**The accumulator half-life is now mis-scaled.** `ACCUM_HALFLIFE_HOURS` is 24, and `evolve.h` documents the recovery guarantee it produces: two days of perfect care lifts a bottomed-out 20 to 80. When Baby was 3 days that was recovery *within* a stage. With a 1-day Baby stage, one half-life now spans the entire stage and two days spans three stages. Propose a rescaled value — halving to 12 h roughly preserves the original ratio of half-life to stage length — recompute both numbers in the `evolve.h` header comment, and update that comment to match whatever you choose.

**Per-stage rate denominators get noisy.** `evolve_scores()` computes `stage_days = days_alive - stage_start_day + 0.5f` and divides `games_played`, `ignored_requests` and `disc_unfair` by it. Over a 1-day Baby stage a single ignored request reads as 0.67/day where it used to read 0.29/day, so the same behaviour scores far worse. Either raise the smoothing constant or floor `stage_days`; say which and why.

**Growth spurts now fire three times in six days.** `GROWTH_SPURT_FRACTION` pulls excess weight back toward `STAGE_BASELINE_*_G` at each transition. Three spurts inside six days may make deliberate overfeeding almost impossible to express, which would put the Chonky Adult form out of reach. Check whether Chonky is still reachable and report the answer.

---

## 2. Save migration

Bump to `SAVE_SCHEMA_VERSION 5`. Follow the established pattern documented in `storage.h`: **append new fields to the tail so v4 stays a strict byte prefix of v5**, then "copy the old blob, zero the tail". Do not remap existing fields.

Budget check before you start: `SAVE_SIZE_BUDGET` is 448 and schema 4 measures a little over `SAVE_V3_SIZE` (397) plus the egg fields. Confirm the remaining headroom, and if the new fields do not fit, say so rather than shrinking something else. The `static_assert` in `storage.h` will fail the build if you overrun — good, leave it.

A dev save already past the new boundaries must migrate cleanly:

- Recompute stage from age under the new boundaries.
- Backfill `stage_day_entered[]` with the boundary days actually crossed, so the timeline stays coherent.
- **Do not replay history.** No duplicate journal entries, no re-fired evolution animation. `evo_announce` is persisted for exactly this reason — honour it, and make sure a migration cannot set it for a transition that already happened.
- **Do not re-run `evolve_pick_form()` for a stage already recorded in `evo_path[]`.** The form the Visitor already has is its actual past.

Preserve: care accumulators, personality traits, `evo_path`, Visit Records, game records, `food_history`, journal.

Extend the `V` console command (currently a v1→v2 test) or add a sibling so the v4→v5 path is actually exercised on hardware. A migration that has never been run is a guess.

---

## 3. Variable visit length

`VISIT_LENGTH_DAYS` (21) is referenced in exactly two places: `farewell.cpp:34` and the `@` console command. Replace it.

```c
#define VISIT_DEPART_MIN_DAY    9.0f
#define VISIT_DEPART_MAX_DAY   16.0f
#define VISIT_DEPART_MAX_DRIFT  0.15f   /* days of movement per re-evaluation */
#define VISIT_DEPART_LOCK_HOURS 36
```

Reference outcomes for the four care profiles:

| Care | Total visit |
|---|---|
| Poor | ~9–10 days |
| Average | ~11–12 |
| Good | ~13–14 |
| Excellent | ~15–16 |

**Map continuously across 9–16, not into four buckets.** Those ranges do not tile the interval — 10–11, 12–13 and 14–15 would be unreachable. Treat the table as expected outcomes for the reference profiles in §10, and let a Visitor depart on day 12.4.

The poor-care band sits directly on the day-9 floor, so "poor" and "very poor" clamp to the same outcome. That is acceptable and deliberate — confirm in your report that it is a clamp and not an arithmetic accident.

---

## 4. Stay quality — reuse the accumulators, do not build a second system

The scoring machinery already exists and is proven. `evolve_scores()` returns `evo_scores_t { cs, ba, ia, engage, ignored_per_day, unfair_per_day }`, where `cs` is a 0–100 care score whose weights already sum to 1.0:

```c
s.cs = 0.30f*care_happy + 0.22f*care_fed + 0.18f*care_clean
     + 0.15f*care_sleep + 0.15f*engage;
```

Those are EMAs over the same `care_advance()` path used live and offline, with the half-life recovery guarantee documented in `evolve.h`. **Derive stay quality from `evolve_scores()`.** Do not add a parallel set of decayed counters — a second scoring system would drift from the first and would double the surface that the offline path has to keep correct.

Suggested shape, to be calibrated:

```c
float stay01 = clamp01((0.80f*s.cs + 0.20f*(s.ba + 100.0f)*0.5f) / 100.0f);
float depart_day = VISIT_DEPART_MIN_DAY
                 + stay01 * (VISIT_DEPART_MAX_DAY - VISIT_DEPART_MIN_DAY);
```

**A naive linear map overshoots — I ran it against the three existing seeds.** Using `diag_seed_care()`'s values and assuming `stage_days ≈ 3.5`:

| Seed | `cs` | `ba` | linear `depart_day` | target |
|---|---|---|---|---|
| `_` POOR | 25.4 | −40 | **10.8** | 9–10 |
| `=` MID | 59.1 | +3.0 | **13.0** | 11–12 |
| `+` EXCELLENT | 91.3 | +39.2 | **15.1** | 15–16 |

Excellent lands; poor and average both come in a day or two high. A shaping curve or a re-centred mapping is required. Show your calibration table in the report using the same three seeds so I can check it against these numbers.

Departure is **not** fixed at the Adult transition — `depart_day` is re-evaluated on a fixed cadence for the whole visit, so late care still moves it. Report the cadence you chose and where it is driven from.

---

## 5. Departure stability

Implement all four:

1. **Clamp drift** to `VISIT_DEPART_MAX_DRIFT` per re-evaluation. One burger cannot buy a day.
2. **Never retroactive.** A recalculation may never place departure in the past or inside the next few hours; clamp forward to a minimum notice window.
3. **Lock on approach.** Within `VISIT_DEPART_LOCK_HOURS` of the projected date, freeze it.
4. **Hints follow the lock.** The foreshadowing lines must begin only after the lock, so they can never start and then stop. Confirm the hint window fits inside 36 hours.

`depart_day` must be persisted, not recomputed from scratch each boot — otherwise the lock means nothing across a power cycle. Add it to the schema-5 tail.

Say in your report whether the projected date is exposed in the UI anywhere.

---

## 6. Departure must be witnessed

`main.cpp` checks `farewell_due()` once per second from `sim_timer_cb` and calls `farewell_begin()` immediately. With a fixed 21-day visit that was harmless. With a variable date it is not: departure can fall at 3am, or during an absence, and `sim_catch_up()` will carry the Visitor past it while the device is in a drawer. The farewell would then fire into an empty room, `visitrec_archive()` would seal the record, and the child would never see the goodbye.

Require: departure executes only when the device is on, the screen is awake and the Visitor is awake (`!p->asleep`). If the moment passes offline or during sleep, hold it and play the farewell at the next real opportunity, with copy that acknowledges the wait. `SF_FAREWELL_ARM` already exists in `save_t.flags` — that is the flag for this. Cap how long it will wait and say what happens at the cap.

---

## 7. Farewell and Visit Record reflect the stay

`farewell_compose()` builds the note from deterministic templates and real history. Extend it with visit length.

- **Long stay:** *"I had such a good time here I didn't want to leave!"*
- **Middle:** *"I had a lot of fun visiting Earth!"*
- **Short stay:** still warm, still funny.

The tone rule in `farewell.h` stands and is the hard one: **never guilt-heavy.** Nothing resembling "you were bad so I'm leaving." A shorter visit is a consequence of the experience, not an accusation. `FAREWELL_MAX` is 180 bytes — check your longest template still fits.

Add to `visit_rec_t`: broad care band, and whether the stay landed at the short, middle or long end of the possible range. `days` is already there. `sizeof(visit_rec_t)` is asserted at 233 in `visitrec.cpp` — update the assert and the arithmetic in the header comment when you grow it.

While you are there: that comment says *"a 21-day visit means eight records is roughly half a year."* With 9–16 day visits, `VISIT_KEEP` 8 is now about three months. Update the comment; leave the value unless you think it should change.

---

## 8. Bathroom pacing — measure before you tune

**Read this whole section before changing `RATE_BATHROOM_PER_HOUR`. The request as stated would make the Adult bathroom need slower, not faster.**

Current config: `RATE_BATHROOM_PER_HOUR` 22.0, `BATHROOM_URGENT_PCT` 70. That is **3.2 hours from empty to urgent**, 4.5 hours to 100%, for every stage. The requested target is:

| Stage | Awake hours to urgent |
|---|---|
| Baby | 2.5 – 3.5 |
| Kid | 3.0 – 4.0 |
| Teen | 3.5 – 5.0 |
| Adult | 4.0 – 6.0 |

The existing 3.2 h already sits inside the Baby band and is **faster** than the requested Adult band. So the reported symptom — "it feels like about once a day" — is not explained by the configured rate. Something else is producing it. Candidates, in the order I would check them:

- `care_advance()` raises `bathroom` only when `!s_bath_active`. If that latch ever sticks, the meter freezes. There is a self-heal in `care_tick()`, but it only runs when `care_tick()` runs.
- Sleep. `SLEEP_START_HOUR` 20 to `SLEEP_END_HOUR` 7 is an 11-hour night, and **the bathroom rate is not reduced during sleep** — see §9. A full cycle completes inside the night, producing the one offline accident and then parking at `OFFLINE_BATHROOM_PARK_PCT`. If most of the meter's travel happens while nobody is watching, the felt frequency during play is much lower than the configured rate.
- `care_fast_forward()` passes `ctx.offline = false`, so console-driven testing does not exercise the offline accident path at all.

**So: instrument first.** Report the measured wall-clock interval between consecutive urgent events during normal awake play before you change any constant. Then implement the stage table — as a per-stage lookup replacing the single constant, all in `config.h` — and tell me whether the real fix was the stage table, the sleep multiplier in §9, or a latch bug.

Randomise the target: draw a fresh duration uniformly from the stage's range at the start of each cycle rather than running on a fixed rate, so timing never feels like clockwork. Use a seedable RNG and report the seed.

**One design risk worth your opinion.** A kid may have the device on for an hour a day. If the meter advances on Visitor-awake time regardless of whether anyone is watching, most bathroom events resolve as offline accidents the child never had a chance to prevent — so raising the frequency could make the feature feel worse. Tell me whether you think a stronger mitigation is needed than what §9 already provides.

---

## 9. Sleep and offline

**Add a sleep multiplier.** `care_advance()` currently applies `RATE_BATHROOM_PER_HOUR` identically whether `ctx->asleep` is true or false — unlike hunger, happiness, energy, cleanliness and weight, which all branch on it. Add `VISITOR_POTTY_SLEEP_RATE` (start at 0.25) and apply it the same way the other rates branch. At 0.25 an 11-hour night adds roughly 60% instead of overflowing twice, which removes the guaranteed overnight accident.

Never wake the Visitor because the bathroom meter moved.

**Correcting an instruction I gave you earlier:** I previously said the meter should be clamped *below* the warning threshold on return from an offline accident. That is wrong and contradicts a deliberate, documented decision in this codebase. `OFFLINE_BATHROOM_PARK_PCT` is 95 specifically because parking at 100 fired a second accident within seconds of boot, and the comment in `config.h` explains it: the Visitor should return "doing the potty dance with the normal grace period left to react." **Keep that behaviour.** `OFFLINE_MAX_ACCIDENTS` stays 1.

What does need checking is whether the grace window is measured correctly across the boot boundary. `s_urgent_since_ms` is a `millis()` value and `millis()` resets to zero on boot, so a Visitor returning at 95% must get the full `BATHROOM_GRACE_MS` from the moment the device comes up — not from a stale or zeroed timestamp. Verify this and report it.

All of this stays on the single `care_advance()` path. Do not add an offline-specific implementation — `sim.h` is explicit about why, and it is the right call.

---

## 10. Warning escalation

More frequent events must not mean more nagging. `care_tick()` already computes urgency and feeds `ui_pet_set_urgency()` a normalised value above `BATHROOM_URGENT_PCT`. Build the tiers on that:

| Meter | Behaviour |
|---|---|
| 0–60% | nothing |
| 60–80% | occasional subtle holding pose, no bubble |
| 80–95% | obvious holding pose + occasional bubble |
| 95–100% | stronger wiggle / clear warning |
| past `BATHROOM_GRACE_MS` | accident |

Note this pushes the holding pose earlier than `BATHROOM_URGENT_PCT` (70) currently allows, so `care_is_holding()` and the urgency normalisation both need adjusting. Per-tier minimum bubble intervals go in `config.h`; the existing `BATHROOM_WARN_MS` (20 s) is one of them. Respect the tier cooldowns in `ui_bubble` — do not bypass them.

---

## 11. Test console

The harness largely exists in `diag.cpp`. Extend it; do not build a parallel one.

Already there and directly useful:

- `+` `=` `_` — seed EXCELLENT / MID / POOR care history via `diag_seed_care()`
- `h` `H` `j` — simulate 8 h / 72 h / 8 days away, through `sim_catch_up()`
- `T` — fast-forward 30 simulated minutes
- `@` — jump to visit end (currently sets `days_alive = VISIT_LENGTH_DAYS`; must be rewritten)
- `F` — force next stage + evolution; `O` — arm the offline evolution reveal
- `<` — `evolve_explain()`; `J` — Visit Records; `X` — reset; `W` — wipe; `Y` — persistence fidelity

To add:

- A **fourth care seed for GOOD** (`diag_seed_care(3)`), between MID and EXCELLENT. The four target bands in §3 need four fixtures and only three exist.
- A **time-travel command** that advances the age clock by N hours through the unified path, so the whole 9–16 day arc is verifiable in minutes. `diag_simulate_absence()` is the model; it must age correctly now that §0 derives age from `hatch_ts` — advancing the clock, not just calling catch-up.
- A **stay-quality report**: `cs`, `ba`, `stay01`, projected `depart_day`, whether the date is locked, and hours remaining. Model it on `evolve_explain()`, which sets the standard for this project — the selection must be explainable, not just correct.
- Bathroom state in `care_report()`: current meter, current randomised target, tier, seed.

Pick unused keys — `x i r o f m b d s W ? 0-9 e w k T R X c l h H j p V Y y N G A u Q q E z a K < > / F O + = _ J @ ; : B S L n P D M [ ] g t v Z` are taken. Update `diag_help()`.

---

## 12. Tests to run and report

**Age clock (§0 — run these first)**
- a continuously-powered device crosses day 1.0 and changes stage without a reboot
- twelve consecutive 2-hour absences advance age by 1 day, not 0
- `pet_age_days()` is fractional and matches `(now − hatch_ts)/86400`
- age never goes backwards after a clock correction

**Aging** — day 0 Baby, day 1 Kid, day 3 Teen, day 6 Adult, checked at the fractional boundary

**Offline** — a jump across several boundaries lands on the correct stage; `stage_day_entered[]` records the actual boundary days, not the day it was noticed; no duplicate evolution announcement across three consecutive reboots

**Migration** — a v4 dev save past the new boundaries migrates cleanly; accumulators, personality, `evo_path`, Visit Records, game records and journal all survive; nothing is announced twice

**Visit length** — report the actual computed `depart_day` per profile, not pass/fail:
- `_` poor → ~9–10 · `=` average → ~11–12 · new good seed → ~13–14 · `+` excellent → ~15–16
- mediocre childhood + excellent adult care moves the outcome up
- excellent childhood + adult neglect does not retain the maximum
- never before day 9, never after day 16
- drift stays inside the clamp; the date freezes at the 36 h lock and survives a reboot
- a departure falling during sleep or an absence waits and plays when witnessed

**Bathroom**
- measured awake interval between urgent events, before and after the change
- Baby cycles faster than Adult; timing varies across repeated cycles
- sleep multiplier reduces the rate as configured; no guaranteed overnight accident
- tiers escalate correctly with their cooldowns
- exactly one offline accident; meter parks at 95; full grace period available after boot

**Regression** — persistence (`Y`), evolution (`<`, `F`), journal, Visit Records (`J`), farewell (`@`, `;`), sleep (`N`/`G`/`A`), food, cleanliness, discipline (`>`)

---

## 13. Non-goals

No new features. No new mini-games, care mechanics, UI pages or audio work. Timing, balance and the departure model only. `loop()` does not grow.

---

## 14. Output contract

Order of work: **age clock (§0) → console/time-travel harness (§11) → config surface → stage timing → migration → stay quality → departure stability and witnessing → bathroom → warnings → tests.**

Then compile, flash, and report:

1. **Final configured values** — every constant this pass touched, as a table
2. **The stay-quality calibration table** — `cs`, `ba` and computed `depart_day` for all four seeds, against my linear-map figures in §4
3. **Bathroom measurement** — the interval before and after, and which of the three candidate causes in §8 was actually responsible
4. **Test results** — the §12 matrix, with the RNG seed used
5. **Answers to:** displayed-age copy that now reads wrong (§1a); whether Chonky is still reachable (§1b); the accumulator half-life you chose and the two recomputed recovery numbers (§1b); whether the departure date is visible in the UI (§5); how long a held departure waits (§6); whether the bathroom session-length risk needs stronger mitigation (§8)
6. **Anything that broke or surprised you**

**Then stop. Do not begin Phase 10.**
