# Phase 9.5 — personality, dreams, identity, refinement

Written alongside the implementation, against the brief given on 2026-08-29.
Baseline for this pass is the tag **`pacing-balance-baseline`** (annotated tag
`30b6fe9` on commit `496cf0c`), which is the accepted pacing & balance pass.

Scope rule for this phase: **no audio and no IMU work.** Phase 10 owns both.
Where Phase 9.5 touches something Phase 10 will finish — the Settings page —
it reserves the control and stops.

---

## 1. Pet Info → Settings

`PAGE_CLOCK` is renamed throughout the player-facing UI. The enum keeps its
name (`PAGE_CLOCK`) because the page order is load-bearing and renaming the
constant would touch the pager, the dots and every diagnostic that indexes
pages; only what a child reads changed.

The page is now the device's home for:

| Control | State |
|---|---|
| Date & Time | live, unchanged |
| age / stage / form / weight / gender | live |
| Volume | reserved, visible, disabled — Phase 10 |
| Recalibrate Tilt | reserved, visible, disabled — Phase 10 |
| Gravity Reactions | reserved, visible, disabled — Phase 10 |

Reserved cards are drawn in full colour and simply do not respond, which is
the same treatment the unfinished Games and Care cards had in Phase 3. An
empty page that sprouts three controls later is a worse surprise than a
visibly unfinished one.

### Age copy

**1 real day = 1 Visitor year, and the child-facing copy now says so.** The
stage boundaries were already defined that way — Kid at 1, Teen at 3, Adult
at 6 — so this is a relabelling, not a second clock. `days_alive` is still
the number; only its label changed.

| Surface | Before | After |
|---|---|---|
| Stats | `Baby - day 6 - 48 g` | `Baby - 6 years old - 48 g` |
| Settings | `day 6` | `age  6 years old` |
| Journal, "This Visitor" | `Day 6 on Earth` | `6 years old` |
| Farewell subtitle | `12.4 days on Earth` | `12 years old` |
| Journal, "Visitors before" | `Sweet - 12 days` | `Sweet Adult - stayed 12 days` |

The Visit Records line keeps **days** because it is a *duration* — how long
the visit lasted — and now says "stayed" so it cannot be misread as an age.
**Diagnostics still print day numbers throughout**: a developer needs the raw
fractional figure and the console is not a child-facing surface.

---

## 2. Dreams

Flavour only. A dream never touches a stat, an accumulator or the evolution
path.

### The seven rules, as ratified

| Rule | How it is met |
|---|---|
| Night dream needs at least 2 h of meaningful sleep | `DREAM_MIN_NIGHT_SEC`, checked against **recorded** sleep |
| Baby nap dream needs at least 20 min | `DREAM_MIN_NAP_SEC`, same check |
| Naps CAN dream once eligible | then a `DREAM_NAP_CHANCE_PCT` (35%) roll |
| At most one dream per sleep period | `SLEEPF_DREAMT`, set on the first decision |
| Repeated reboots in one night cannot duplicate | the period's state is **persisted**, so a reboot resumes it rather than starting a new one |
| Eligibility uses recorded duration, not "is the clock in a sleep window" | `sleep_accum_sec` accumulates in `care_advance()`; the window only opens and closes the period |
| Thresholds configurable | all four constants live in `config.h` |

### Why it is built around a SLEEP PERIOD

The first implementation dreamt on the `asleep -> awake` state transition,
and that got four things wrong at once:

- it never asked **how long** the Visitor had actually slept, so a two-minute
  bedtime produced a dream;
- a boot in the middle of the night and the real morning wake-up were two
  separate transitions, so **one night could produce two dreams**;
- a reboot restarted the bookkeeping entirely;
- `asleep` is a momentary flag that scripted actions clear, and that
  `sim_catch_up()` deliberately clears at the end of every boot ("you are
  here now").

A dream is now a property of a **sleep period**, and the period is explicit
and persisted: `sleep_accum_sec` (sleep actually recorded in it),
`SLEEPF_NAP` (decided when it opened), `SLEEPF_DREAMT`.

Two signals that used to be one:

- `ctx.asleep` - the Visitor is in bed. Drives the **stat rates**.
- `ctx.sleep_window` - the **clock** says this is a sleep period. Opens and
  closes the period.

Conflating them is exactly what made a boot at 2 am close the night and open
a second one.

Accumulation happens in `care_advance()`, the one shared path, so sleep
recorded during an absence counts identically to sleep in front of the child,
and a period spanning both is still **one** period. The dream is only
*recorded* when the period closes - which may happen mid-catch-up with no
screen to speak to - and `pending_dream` carries it until the live tick can
tell it, deferred so it queues behind any return greeting.

There is consequently **no dream hook in `main.cpp` or in the console's
absence command any more**. One rule, one place.

### Verified on hardware

```
night, 1 h slept   -> silent        night, 1h59m -> silent
night, 2 h slept   -> DREAM         night, 9 h   -> DREAM
nap,   5 min slept -> silent        nap,  19 min -> silent
nap,  25 min slept -> 42 of 100 dreamt   (35% target)
9 h night, ALREADY dreamt -> silent
three reboots inside one night -> period stayed OPEN (150/152/152/153 min),
                                  dream count unchanged, then exactly ONE
                                  dream when morning came
```

24 dreams, selected by **weight** rather than a flat roll: personality traits,
favourite food, whether games have been played, whether there have been
accidents, and stage. The dream immediately before is excluded, so the same
dream never lands two mornings running.

**Each dream is stored as one byte** - an index into the table - which is why
every line is self-contained with no runtime substitution. A dream about
burgers assembled from "favourite food" would silently become a dream about
fruit the day the Visitor's favourite changed. The Journal keeps the last
three in a longer first-person form.

---

## 3. Journal "About Me"

First person, in the Visitor's own voice, assembled from what actually
happened rather than from one template with holes in it. Five conditional
parts, each included only when there is something true to say:

1. both personality traits, always;
2. favourite food (always answerable), and favourite game **once a game has
   been played**;
3. behaviour, from the real discipline history — and gentle in both
   directions;
4. **one** specific funny detail, ordered so the most unusual thing about
   this Visitor wins: cakes, then accidents, then forgotten lights, then games,
   then meals;
5. how it turned out, from the current form.

A brand-new Baby gets two honest sentences; an Adult with a history gets five.

---

## 4. "How I grew up" — the real path

**This was broken, and it was broken in two independent ways.**

`evo_path[]` had exactly one writer, in `sim_catch_up()` — the *offline* path.
A device left switched on evolved through `care_tick()` and recorded nothing.
That writer was also guarded by `if (p->stage < 4)`, and `STAGE_ADULT` **is**
4, so even an offline evolution could never record the final adult form. The
Journal then printed a hardcoded `"Baby"` and whatever the array happened to
hold, which for most Visitors was nothing.

Fixed by `pet_record_form()` — one function that knows the slot mapping,
called from every path that can change stage or form: the live tick, the
offline catch-up, the mid-adult glow-up re-check, `evolve_present()`,
`evolve_on_stage_entered()`, and the hatch. Writing the same form to the same
slot twice is a no-op, which is what makes "no duplicate stages" true by
construction — **the slot *is* the stage.**

Saves written before this are repaired at load by `pet_backfill_evo_path()`,
which fills only what is **knowable**: the Baby (there is one Baby form) and
the current stage (it is the form on screen). An intermediate stage that was
lived through but never recorded stays blank and the Journal says
`(not recorded)`. Re-running `evolve_pick_form()` to fill it would write a
guess made from *today's* accumulators into the Visitor's past, which is
exactly what the migration rules forbid.

`FORM_BABY` is 0, so a zero in slot 0 is ambiguous between "Baby" and "never
recorded". It does not matter and readers treat index 0 as Baby
unconditionally — every Visitor was a Baby, and there is only one Baby form.
For Kid, Teen and Adult, form 0 is not a legal value, so zero there genuinely
does mean "not recorded".

Rendered vertically, because it is a ladder and not a sentence:

```
Baby
 |
 v
Good Kid
 |
 v
Bright Teen
 |
 v
Chonky Adult
```

Adult forms are stored as bare adjectives ("Chonky") because that is how the
evolution tables read best; `forms_long_name()` adds the tier for places like
this where it would otherwise lose the last rung.

The same `evo_path[]` is what `visitrec_archive()` copies, so Visit Records
and the Journal now agree because they are the same data — and now that data
is actually populated.

---

## 5. Lights reactions, and deferred reactions generally

Switching the room light off while the Visitor is **awake** does not put it to
sleep. Nothing in this pass touches the sleep model; bedtime remains the
clock's job. It stays awake in a dark room and complains, with
personality-specific variants (dramatic, shy, mischievous, curious, and a
grumbly-not-cruel Grumpy). Lights back on gets an optional line, 70% of the
time — always commenting on it makes the switch feel like a toy.

`care_set_lights()` is called from four places that are **not** the player:
the save loader, the morning restore, `care_reset()` and the offline model.
The reaction therefore lives in a separate entry point,
`care_player_toggle_lights()`, rather than behind a heuristic inside the
setter guarding four callers.

### The deferred-reaction rule

`ui_bubble_say()` refuses everything while a menu or game is open. That is
right for chatter and **wrong for a reaction the player deliberately
triggered**: the light switch is on the Care page, so the Visitor's answer was
spoken to a covered screen and its timer ran out behind the menu.

`ui_bubble_say_deferred()` shows it immediately when the pet screen is
visible, and otherwise queues it and shows it once the screen is back — **with
its duration starting then.**

Bounded hard, so there is no queue buildup and no leak:

- `BUBBLE_DEFER_SLOTS` = 3 fixed slots, no allocation; oldest dropped when full
- text is **copied**, not held by pointer (dream lines are stack buffers)
- anything held longer than `BUBBLE_DEFER_HOLD_MS` (120 s) is discarded rather
  than surfacing minutes late and out of context
- the farewell drops the whole queue — a light-switch joke landing on top of a
  goodbye note would be the worst possible timing

Applied to every reaction the player presses a button for and is meant to
watch: the lights, "I don't need to go!", "It's already clean!", "It's still
an egg!", the hatch greeting, and the post-absence dream.

---

## 6. Old-accident comments

Gated on a poop that has **already grown its stink lines**
(`STINK_AFTER_MS`), so the joke always refers to something visible on screen.
`POOP_COMMENT_GAP_MS` 5 minutes between comments plus a 45% roll — a gag on a
one-minute timer is nagging.

The cooldown runs between *comments*, not from the last time the floor was
clean, so the first remark can land as the stink appears rather than five
minutes after it.

Variants by trait **and by form**, because the brief named both kinds of fact
(Mischievous and Curious are traits; Grumpy, Sweet, Scruffy and Chonky are
adult forms):

| | |
|---|---|
| generic | "Why does it stink in here?" · "Uh... did I do that?" |
| Mischievous | "Wasn't me." · "That was there when I arrived." |
| Dramatic | "THE SMELL HAS RETURNED." |
| Grumpy | "Seriously. Clean it." · "Hmph. Smelly." |
| Curious | "What IS that smell?" |
| Sweet | "Sorry about the smell." · "No rush though!" |
| Scruffy | "Smells like home!" · "I've decided to call it Kevin." |
| Chonky | "That smell is putting me off my snack." |
| Tidy | "I have made a list. It has one item." |

Nobody is blamed. The funniest version is the one where the Visitor is not
entirely sure it was responsible.

---

## 7. Personality dialogue generally

All of it moved into one module, `dialogue.cpp`. Before this, lines lived
wherever the event that triggered them lived — sleepy pokes in `scr_main`,
told-off lines in `discipline`, four fixed lines per tier in `strings.cpp`.
That is fine for twelve lines and stops being fine the moment personality has
to colour eight kinds of moment, because "several variants so one line does
not repeat constantly" is a property of the *whole line set*, and you cannot
see the whole line set when it is scattered.

Covered: dreams, lights on/off, old messes, waking, being poked while asleep,
mischief, being told off, food (yum / half-eaten / refused), after a game, and
the first words after hatching. Every selector falls back to a generic pool,
so a Visitor whose traits have no special line still gets a good line rather
than silence. A trait pool wins 70% of the time, not always, so a Visitor
still surprises you occasionally instead of having exactly one voice.

Tone rules, unchanged and non-negotiable: kid-friendly, **Grumpy is grumbly
and never cruel, no form is ever mean or scary**, nothing blames the child,
and short.

One line that mattered more than the rest: the **food refusal**. Refusing food
when genuinely full is *not* misconduct, so every variant has to sound full
rather than defiant — otherwise a child will reasonably reach for Discipline.

---

## 8. Discipline opportunities

Two separate changes that happen to meet in the same roll.

### Frequency is stage-weighted

Required order **Kid > Baby > Teen > Adult**, as `MISCHIEF_W_*`:

| Stage | Weight | Effective rate for the seed Visitor |
|---|---|---|
| Kid | 100% | most |
| Baby | 85% | second |
| Teen | 62% | third |
| Adult | 40% | least |

**Babies are no longer blameless.** They used to be excluded outright, which
removed the whole mechanic from the first day of every visit — the day a child
is most likely to be watching.

The weight **multiplies the finished percentage**, not the base term. Applying
it to the base alone (the first draft) let the shared additive terms swamp it:
a Kid at 21% and an Adult at 12% is technically the right order and nothing
like the intended difference. Multiplying at the end preserves the ordering by
construction — the weights are positive and monotonic — and keeps an Adult
with a bad history meaningfully wilder than an Adult with a good one.

The console's `>` report prints the table using **the same function the roll
uses**, evaluated per stage, so the printed ordering cannot drift away from
the real one. (A hand-copied duplicate of a live expression has already caused
one wrong calibration in this project.)

### Behaviour is learned, and always recoverable

`pet_state.learned_mischief` is a rolling EMA over how windows were *resolved*
— corrected pulls toward 0, ignored pulls toward 100, `LEARN_ALPHA` 0.22 per
event, starting neutral at 50. It feeds back into the roll centred on
`LEARN_START`, scaled by `LEARN_WEIGHT_PCT`, so a neutral history is worth
nothing either way.

Because it is an EMA and not a counter, **nothing is locked in**: a Visitor
written off as a Kid can be brought round as a Teen, and a perfectly-raised
Kid whose Teen years are ignored drifts back. On a child's device that
recoverability is the point, not a nicety. The `}` console command
demonstrates it rather than asserting it — five ignored windows then five
corrected ones — and restores the value afterwards so the demo does not alter
the Visitor.

### Pacing and variety

- roll every 15 s (`MISCHIEF_CHECK_FAST_MS`)
- a **randomised** 60–180 s gap after each opportunity, redrawn each time —
  a fixed gap makes a mischievous Visitor a metronome
- the gap is redrawn *before* the window opens, so back-to-back mischief is
  impossible even if it is corrected instantly
- a naturally mischievous Visitor in active play lands roughly one every
  1–3 minutes; a calm one is far rarer because the roll keeps failing, not
  because the gap is longer

### Every opportunity is legitimate

Each candidate carries its own precondition, so a Visitor never throws food it
is hungry for and never refuses a bedtime that is not due:

| Kind | Requires |
|---|---|
| drops food on purpose | hunger ≥ 55 — a hungry Visitor turning down dinner is a *need* |
| tiny deliberate mess | floor not already full |
| bedtime refusal, for fun | inside the sleep window, and awake |
| cake begging | hunger ≥ 45 — never a disguised hunger cue |

Personality nudges **which**, never **whether**: a tidy Visitor rarely makes a
mess, a foodie begs for cake more.

**Unchanged and non-negotiable:** hunger, tiredness, dirt, a bathroom accident
caused by an ignored need, and refusing food when genuinely full are never
misconduct and never open a window.

---

## 9. Pre-hatch identity — Surprise colour and gender

"Random" is now **Surprise**, everywhere the player can see it.

**Colour:** Red, Purple, Blue, Green, Teal, Yellow, Surprise. The Surprise
swatch is drawn as the six colours themselves in stripes, not a "?" — "?" read
as *no colour*; six stripes read as *it could be any of these*. The egg itself
is drawn with a pale shell and six rainbow spots while Surprise is selected,
and **the resolved colour stays hidden until the hatch**: the display palette
comes from `egg_choice`, never from `egg_color`.

**Gender:** Boy, Girl, Surprise.

Both surprises resolve **once**, when START is pressed, and are persisted
immediately with a forced save — the identity is on flash before the
five-minute timer is allowed to run, so a power cut brings back the same egg
and the same Visitor rather than rerolling either.

**Gender is identity and presentation only.** It is held to exactly the same
standing rule the egg colour has had since Phase 9: it never reaches an
accumulator, a form choice, a care rate, a discipline roll or the evolution
path.

---

## 10. Hatch reveal, then first words

Three beats, strictly in order, never overlapping:

1. the shell opens and the Baby appears;
2. **if** gender was a Surprise, a banner reads "It's a boy!" / "It's a girl!"
   for 3.2 s — a banner rather than a speech bubble, because bubbles are
   preemptible, cooldown-gated and anchored to a Visitor that is mid-hatch
   animation, and this is the one moment that must not be refused, moved or
   talked over;
3. after it clears, the Visitor's first ever words — "Hi!", "Is this Earth?",
   "Are you my person?", "I think I live here now."

Beat 2 is skipped when the player already chose. Beat 3 always runs.

**If the reveal is missed**, the Journal's first card opens with `It's a boy!`
on its own line, above everything else.

---

## 11. Pre-hatch touch layout

The screen gained a gender row, so the layout was re-derived rather than
squeezed. The egg preview moved up to `EGG_ROOT_Y` — at the old `PET_HOME_Y`
the shell was drawn *underneath* the pickers: present in the object tree,
invisible in practice.

```
  egg preview   root y = -6; the shell spans   30 .. 148
  colour row 0  156 .. 208     (4 swatches, 80 x 52)
  colour row 1  214 .. 266     (3: teal, yellow, Surprise)
  gap                           266 -> 292 = 26 px
  gender row    292 .. 344     (3 buttons, 108 x 52)
  DEAD SPACE                    344 -> 370 = 26 px
  START         370 .. 442     (72 tall, 6 px clear of the 448 panel)
```

Row 0 is 4×80 + 3×10 = 350 px in a 368 px panel (9 px a side); the gender row
is 3×108 + 2×12 = 348 px (10 px a side).

Both gaps are **26 px**, above the 20–24 px the brief asked for, and both are
now `static_assert`ed in `scr_main.cpp` — the last bug on this screen was a
4 px *overlap* that looked completely fine, and numbers checked only by eye
get broken by the next layout change.

A touch that begins on a selector can never fire START, for two independent
reasons: the 26 px gap, **and** the fact that LVGL delivers `CLICKED` to the
object the press began on. Both matter; the gap alone was not the old bug.

Selection state stays obvious: a 5 px white ring on the chosen colour and the
chosen gender.

---

## 12. Save schema 8

`sizeof(save_t)` = **433 bytes**, budget 448 - verified on hardware.

Phase 9.5 bumped the schema **twice**: 7 for the identity and dialogue state,
then 8 when the ratified dream rules required the sleep period to be
persisted. Both hops have console fixtures and both have been run.

Appended, as every schema has been, so the migration stays "copy the old blob,
zero the tail":

```
v6 417
v7 427 = 417 + 10   (gender, gender_choice, learned_mischief,
                     dream_id[3], dream_n)
v8 433 = 427 +  6   (sleep_accum_sec, sleep_flags, pending_dream)
```

`SAVE_V6_SIZE` 417 and `SAVE_V7_SIZE` 427 are frozen, and `static_assert`s
prove both appends at build time — cheaper than discovering
on hardware that every v6 save reads as corrupt, which is exactly how the
`SAVE_V4_SIZE` mistake was found.

**The zeroed tail is not correct for two of the three new fields**, so every
migration branch seeds them explicitly:

- `gender_choice` becomes **SURPRISE**, because the player was never asked.
  Leaving it at BOY would tell an existing Visitor's owner something the game
  never asked them and never showed them. `gender` is rolled once and then
  persisted like any other resolved surprise.
- `learned_mischief` starts **neutral (50)**. Zero would mean "every
  discipline window this Visitor ever had was corrected" — an unearned reward
  for a history we do not have.
- a zero dream ring **is** correct: no dreams have happened.

15 bytes of headroom remain, still enough for the Phase 10 volume, tilt and
gravity settings.

The schema-8 tail is the one case in this project where a **zeroed** tail is
exactly right: no sleep period is open, none has accumulated, and nothing is
waiting to be said. The migration says so out loud rather than staying
silent, because every other field this phase added needed seeding.

All four hops run on hardware: v1, v5, v6 and v7 all reach v8 with the
accumulators, personality, evolution path, journal, locked departure
projection and the whole schema-7 identity tail intact.

---

## 12b. Bugs found BY the verification pass

Four defects surfaced only on hardware, which is the point of running it there.

**1. The deferred queue retried at frame rate.** `defer_flush()` called
`ui_bubble_say()` from every 10 Hz animation frame and let it refuse. It
worked — the bubble appeared the moment the cooldown expired — but it printed
one `BUBBLE SUPPRESS ... (global cooldown, 4116 ms left)` line every 100 ms
for the whole wait, and every refusal incremented the suppressed-bubble
counter. So a single deferred line inflated by eighty the very statistic the
`S` stress test uses to prove the queue is not leaking. The cooldowns are now
consulted **before** asking, using the same state `ui_bubble_say()` uses, and
anything else is retried at 500 ms.

**2. The no-repeat history held pointers into reusable buffers.** This was
the one I wrote down as a deliberate trade-off — "affects only whether one
line is allowed to repeat, never memory safety" — and I was wrong about the
consequence. `remember()` stored a pointer *into a deferred queue slot*; the
slot was later reused for a different line; `recently_said()` then compared
the new text against a pointer aliasing the same buffer and always matched.
Observed on hardware: a queued lights reaction was released, and the very
next lights reaction was refused **permanently** as a repeat it never was.
`s_recent[]` now owns copies (5 × 96 bytes). Related: once both cooldowns are
pre-checked, the only thing left that can refuse is the no-repeat rule, which
will not clear on its own — so a refused entry is now dropped once rather
than retried 240 times.

**3. The `:` console command bypassed the real START path.** It set
`egg_hatch_ts` directly, so it never resolved the Surprise colour or gender —
the one command anyone would reach for to test hatching was driving a path
the product does not have. `scr_main_egg_start()` is now shared by the button
and the console.

**4. The previous-Visitor callback replaced the first words.** Phase 9's
"They said you keep this place really clean!" was chosen *instead of* the
Visitor's own greeting whenever there was history — so on every device after
the first visit, the Visitor never actually introduced itself. Both now
happen, greeting first.

**5. A newly hatched Baby committed mischief during its own reveal.**
`discipline_init()` leaves `s_last_mis_ms` at 0, so the very first roll after
any fresh start is eligible. Observed: the "It's a boy!" banner appeared and
a mischief bubble ("Look what I made!") took the screen during it, pushing
the Visitor's own first words into the deferred queue. The first thing that
ever happened to that Visitor was a discipline window, before it had said
hello. `discipline_settle()` now holds the spontaneous roll for two minutes
after a hatch and one minute after boot. Mischief the player *triggers* is
unaffected.

**6. The old-mess comment was dead for the first five minutes of every
boot.** `s_last_stink_ms` is a static starting at 0, and the gate is
`millis() - s_last_stink_ms < POOP_COMMENT_GAP_MS`. Zero is not "long ago"
here — it is *time zero* — so for the first 300 seconds after switch-on the
comparison is true against a zero anchor and the comment could never fire.
It happened to produce a quiet period after boot, which is behaviour worth
having, but by accident. `care_init()` now seeds the anchor with `millis()`
so the same quiet period is deliberate and the arithmetic means what it says.
The same misreading made the feature untestable: a probe that set the anchor
to 0 was setting it *forward*, which is why the first ten-run probe scored
0/10. It scores 6/10 against a 45% target once the anchor is real.

**7. A 96-second reboot at night produced a dream.** The offline gate asked
only whether any non-nap sleep chunk had occurred, so switching the device
off and on again in the evening produced a dream every time. It now requires
`DREAM_MIN_NIGHT_SEC` (2 h) of night sleep. The gate lives in
`sim_dreamt_a_night()` with one expression and two callers, because the
console's simulated-absence command had **no dream hook at all** — which is
precisely why a too-permissive gate survived until it turned up in a real
boot log rather than in a test.

An eighth was found by inspection rather than on the device: `dialogue_about_me()`
used the project's usual `n += snprintf(buf + n, sizeof(buf) - n, ...)` idiom,
which underflows at the boundary (`len` is `size_t`, `n` is `int`, so once `n`
passes the buffer size the subtraction becomes ~4 GB and the next call writes
straight past the end). The longest reachable paragraph measured ~280
characters against a 320-byte buffer — a margin of two words. It now uses a
clamped `app()` helper.

---

## 13. Console commands added

```
  U  dialogue samples + About Me      I  identity / growth / behaviour
  ~  force a night dream              ^  ten nap-dream rolls
  (  deferred-reaction test           )  old-mess comment samples
  }  learned-behaviour recovery demo
  #  v6 -> v7 migration test          {  v5 -> v7 (the older chain)
  >  now prints the stage-frequency table and the learned record
```

`^` prints a *distribution* (how many of ten naps dreamed) rather than one
sample, because "sometimes, not always" is a claim about a distribution.
`(` opens the menu, fires a reaction into it, shows the queue holding it and
closes the menu — the whole failure the mechanism exists to fix.
