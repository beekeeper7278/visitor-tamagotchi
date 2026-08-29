# Phase 4 — Food, Care, Weight, Cleanliness: captured requirements

**STATUS: NOT IMPLEMENTED. This is a requirements record, not a design and
not a spec to build from yet.** Captured 2026-08-28 during Phase 2 acceptance
so the intent is not lost between phases. Phase 4 begins only after Phase 3 is
confirmed on hardware.

Numbers are given only where they were actually stated. Where a value would
have to be invented it is marked **[OPEN]** rather than guessed — the point of
this file is to preserve what was decided, not to quietly decide more.

---

## 1. Bathroom need

**Holding pose and warning behaviour.** As the bathroom need builds, the
Visitor adopts a visible "holding it" pose and warns before anything goes
wrong. The player should be able to see a problem coming rather than only
discovering the result.

- Renderer hooks that already exist: a dedicated animation state alongside
  `PET_ANIM_*`, and the existing face parameters (`MOUTH_WOBBLE` and
  `BROW_WORRIED` were built in Phase 2 and are unused so far).
- Bubble tier: `BUBBLE_T0_CRITICAL` — the tier exists and preempts correctly.
- **[OPEN]** the need threshold at which the pose starts, and how long the
  warning window is before an accident.

**Bathroom button behaviour.** Pressing Bathroom on the Care page makes the
Visitor **run off-screen, return about 2 seconds later, and deliver a funny
relief bubble.**

- Run-off/return is a new animation. The existing walk lerp moves within
  screen bounds only; this one deliberately leaves the panel and comes back.
- The pet is absent for ~2 s. **[OPEN]** what the screen shows meanwhile —
  empty room, or a door/effect.
- The relief line is comic in tone. Written into `strings.cpp` with the rest
  of the dialogue, per the one-file rule.

**Ignored bathroom need creates an accident.** If the need is not resolved in
time, the Visitor has an accident, which becomes a persistent mess (§3).

- `save_t` already carries `mess_count` and `pending_need` / `pending_need_ts`.
- This is an *ignored request* in the sense the design already defines, so it
  should feed `ignored_requests` and therefore the care accumulators.

---

## 2. Food

**A completely full Visitor can refuse or drop burger or fruit.** Refusal is a
visible behaviour, not a silent no-op: the offered item is declined, and a
dropped item becomes floor litter (§3).

**Cake is always eaten regardless of fullness**, but gives only a **modest
hunger benefit**.

- Cake is the one item that cannot be refused. That is what makes it the
  vector for overfeeding and for the Chonky adult form.

> **CONFLICT TO RESOLVE BEFORE IMPLEMENTING.** The original brief's Overfeed
> row reads: *hunger ≥ 85, **burger or cake** → hunger + 1/3 normal,
> cleanliness −15, spawn mess, happiness −3, bubble "I CAN'T EAT ALL THAT!"*.
> The requirements above split that differently: **burger and fruit** may be
> refused/dropped when full, while **cake is always accepted**. The two
> descriptions disagree about which items a full pet will accept. The newer
> statement is the more recent instruction, but this needs an explicit
> decision at the start of Phase 4 rather than one reading being silently
> picked. Do not merge them by guessing.

**Weight gain must accumulate gradually.** Weight gain from cake and from
overeating must build up over time and must **not** cause large immediate
changes in body size.

- The renderer already scales `body_w` by ±20% from `pet_live_t.weight_norm`
  (`PET_WEIGHT_SCALE_PCT`), and it updates every frame — so a large jump in
  the underlying value *would* be immediately and obviously visible. The
  constraint is therefore on the rate at which weight changes, not on the
  renderer.
- Existing passive rates: `RATE_WEIGHT_AWAKE` −0.15/h, `RATE_WEIGHT_ASLEEP`
  −0.05/h.
- **[OPEN]** grams per cake, per overfeed, and the mapping from `weight_g` to
  `weight_norm`. Whatever is chosen, a single cake must not visibly resize the
  pet — a per-meal cap or a smoothed approach toward a target is likely, but
  that is a Phase 4 decision.

---

## 3. Room messes and cleanliness

**Leftover/dropped food and accidents persist on the room floor and
progressively reduce cleanliness.** Messes are not instantaneous penalties;
they sit in the room and keep costing cleanliness until cleaned.

- Sources: a dropped/refused food item, and a bathroom accident.
- `scr_main`'s `room_layer` exists for exactly this and is currently empty.
  §9 budgets a **mess sprite pool of 4, hidden** — so the on-screen count is
  capped at 4 even if more accumulate in state.
- Mess sprites are floor objects, so they are a reasonable early PNG asset.
  The seam (`pet_overlay` / `assets.h` / `tools/convert_assets.py`) is wired
  and empty, and Pillow is not yet installed on the build machine.
- Existing state: `save_t.mess_count`, `times_dirty`, `RATE_CLEAN_AWAKE`
  −2.0/h, `RATE_CLEAN_ASLEEP` −1.0/h.
- **[OPEN]** the additional cleanliness drain per mess per hour, and whether
  it stacks linearly with `mess_count` or saturates.

**Care page gets a Clean action that removes room messes.** Consistent with
the existing design: §4 page 4 (Care) already lists four buttons — Bathroom,
Clean Up, Lights, Discipline. This confirms Clean Up's job is clearing the
room, not only topping up a cleanliness stat.

- **[OPEN]** whether Clean Up removes all messes at once or one per press.

---

## Existing hooks this will use

Nothing below needs inventing; it is already in the codebase.

| Hook | Where | For |
|---|---|---|
| `mess_count`, `times_dirty` | `save_t` | mess persistence |
| `pending_need`, `pending_need_ts` | `save_t` | bathroom need + timeout |
| `ignored_requests` | `save_t` | ignored-need accounting |
| `food_count[3]` (burger/fruit/cake), `junk_meals`, `meals`, `last_meal_ts` | `save_t` | feeding |
| `weight_g` | `save_t` | gradual weight |
| `room_layer` | `scr_main.cpp` | mess sprite pool (4, hidden) |
| `pet_live_t.weight_norm`, `.cleanliness` | `ui_pet.h` | already drive the renderer |
| `BUBBLE_T0_CRITICAL` | `ui_bubble.h` | need warnings |
| `MOUTH_WOBBLE`, `BROW_WORRIED` | `forms.h` | holding/worried face |
| `RATE_CLEAN_*`, `RATE_WEIGHT_*` | `config.h` | passive rates |

## Not decided here

Every **[OPEN]** above, plus the food/overfeed conflict. These are Phase 4
decisions and should be made with the pet stat model in front of you, not
pre-committed from a Phase 2 acceptance conversation.
