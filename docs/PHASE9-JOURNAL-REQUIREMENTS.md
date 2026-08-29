# Journal, Visit Records and the farewell note — captured requirements

**STATUS: NOT IMPLEMENTED.** Recorded 2026-08-28. Owned by the Journal phase
(roadmap item 7 in the updated ordering). Nothing here is built yet.

## Placement and scrolling

The Journal stays **one of the six horizontally swipeable main pages**. Inside
it, browsing is **vertical**.

This is the load-bearing constraint: the page pager owns horizontal gestures,
so the Journal's own content must scroll vertically or the two will fight.

- The pager commits a swipe mid-drag once `|dx| >= SWIPE_MIN_TRAVEL_PX` and
  `|dx| > SWIPE_AXIS_RATIO * |dy|`. A predominantly vertical drag therefore
  never reaches the horizontal threshold, so an `lv_obj` with vertical scroll
  enabled should coexist with the pager without special-casing.
- **Verify this on hardware rather than assuming it.** The axis ratio was
  tuned for page swiping, not for rejecting deliberate vertical drags on a
  scrollable child, and a diagonal flick is the case that will break it.

## Contents

- current Visitor journal / history
- previous Visitors — the Visit Records
- character / form
- arrival and departure dates
- memorable milestones and events
- favourite food
- favourite game
- a useful care-history summary
- the saved farewell note

`save_t` already carries `journal[24]` (8-byte entries: ts, type, arg, value)
with classes `JRN_MILESTONE` / `JRN_RECORD` / `JRN_FLAVOUR`, plus
`food_count[3]`, `total_games`, `player_high[4]`, `pet_high[4]`,
`maze_best_ms`, `cakes_eaten`, `lights_forgotten`, `times_dirty` and
`visit_index`. Favourite food and favourite game are derivable from those
counters rather than needing new state.

**[OPEN]** the farewell note is free text and will not fit the 8-byte journal
entry format. Where it is stored, and its length budget, is undecided —
`NVS_VISIT_RECORDS` is 5 and `save_t` is 332 B of a 384 B budget, so this
likely needs its own storage decision and a schema bump.

## The farewell note

When a Visitor's stay ends it leaves a **personalised note describing its time
on Earth**, covering:

- what the user did especially well
- memorable or funny parts of the visit
- things the user could improve next time
- a personality appropriate to that Visitor

**Tone: warm, funny, kid-friendly — never scolding.** The "could improve"
section is the one that will go wrong if written carelessly: it must read as
friendly encouragement from a creature that liked its stay, not as a report
card. A five-year-old who neglected the pet should still want to read it.

Displayed **prominently during the farewell sequence** and **permanently saved
in that Visitor's Visit Record**, so it is re-readable from the Journal long
after the Visitor has gone.

**[OPEN]** whether the note is assembled from templated fragments selected by
care statistics, or from a small set of whole variants per personality. The
first scales better; the second reads better. Decide with the real care data
in front of you.
