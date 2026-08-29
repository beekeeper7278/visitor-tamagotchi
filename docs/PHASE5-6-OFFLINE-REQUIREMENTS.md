# Phase 5+6 — offline continuation requirements

Captured 2026-08-28. These are requirements for the combined Phase 5+6 pass,
not a future phase: the Visitor must keep living while the device is off.

## The core rule

On boot, use the **trusted** RTC to find how much real time passed since the
last valid simulation timestamp, and reconstruct the gameplay state that
would reasonably have changed. Not just an older pet - a pet whose day
actually happened.

## Time is never capped; consequences are

**Uncapped, always the full elapsed interval:**
calendar time · age · days alive · stage/day transitions · visit duration ·
the sleep/wake periods that occurred inside the interval.

**Capped independently, so the pet is always recoverable:**
hunger penalty · happiness/loneliness · cleanliness · number of unattended
bathroom accidents.

This replaces the single `SIM_ELAPSED_CAP_SEC` idea with per-stat caps. The
old constant capped *decay*, never time - but one global number cannot express
"age fully, starve only so far", which is exactly what is wanted here.

## Per-stat rules

**Hunger** keeps falling while off; the pet can return hungry. The total
offline drop is capped so a long absence is never unrecoverable.

**Happiness** declines gently. A few days off must NOT take it from full to
zero. A long absence may leave the Visitor grumpy/lonely and colour the return
greeting, but recovery must be easy.

**Bathroom** need keeps rising. If it crosses the accident threshold during
the absence: create the accident, persist the mess, apply the cleanliness
consequence. **At most ONE unattended accident per offline interval** - then
satisfy the need rather than producing poop after poop in an empty room.

**Existing messes survive.** Dropped food, half-eaten food and accidents that
existed at shutdown are still there at boot, with their real age advanced:
an old poop may already show stink lines, old food may have accumulated its
capped penalty. **Exact type and location preserved** - never respawned as
generic new messes.

**Cleanliness** keeps draining from existing messes using the same rules as
live play, plus any new accident, with the total offline damage capped.

**Energy and sleep** are reconstructed: during scheduled sleep energy
recovers, with Lights ON recovering worse than Lights OFF, and the
forgotten-lights history updating. During awake periods normal drain applies.
Baby daytime naps are reconstructed too.

**Weight does not drift from absence alone.** It follows food and history
rules only. If nothing happened to justify a change in an interval, leave it
alone.

## Return greeting

One primary bubble, highest-priority matching condition only - never stacked.
Roughly: bathroom accident (embarrassed/funny) · very hungry ("I'm
starving!") · mess present (comments on it) · long absence ("WHERE HAVE YOU
BEEN?!") · woke up well (sleepy) · short absence (normal).

Funny and kid-friendly. **Never guilt-heavy** - a child who left the device in
a drawer for a week should still want to pick it up again.

## Architecture

**One code path** for live ticks and offline catch-up. Two implementations of
the hunger/cleanliness/sleep rules is the failure mode this is written to
prevent: the offline path runs rarely and would rot silently.

Elapsed time is processed in chunks so sleep transitions and time-of-day
boundaries land correctly - a single multi-day dt would apply one wrong rate
across the whole gap.

## Required boot diagnostics

Printed after every catch-up: trusted elapsed seconds · age change · hunger,
happiness, cleanliness, energy and bathroom before -> after · whether an
accident occurred · number and types of persisted messes · sleep periods
reconstructed · which negative caps were reached, if any.

The caps line matters most: without it, "the pet came back starving" and "the
pet came back at the hunger floor" are indistinguishable in testing.
