# Phase 2 — Pet Sprite, Animation, Speech Bubbles

**Status: PASS — accepted 2026-08-28.** Baseline tag: `phase2-pet-baseline`.

Scope was the main pet screen only: the procedural renderer, the Baby form,
idle behaviour, expressions and the speech-bubble policy. No menu, no stats,
no care mechanics, no RTC, no games, no evolution, no journal.

## What was built

| Module | Role |
|---|---|
| `include/forms.h` | `pet_form_t` and all 12 form IDs (1 Baby + 2 Kids + 3 Teens + 6 Adults). **Baby populated only.** |
| `src/ui_pet.*` | Procedural renderer + animation state machine. |
| `src/ui_bubble.*` | Speech-bubble manager. |
| `src/strings.*` | All dialogue, one file. |
| `src/scr_main.*` | Resident pet screen. |
| `include/assets.h`, `tools/convert_assets.py` | PNG seam — wired, empty. |

## Key decisions

**Object composition, not a canvas.** A 160x160 `TRUE_COLOR_ALPHA` canvas is
102 KB and needs a full blit per frame; ~20 styled primitives cost ~2 KB of
LVGL heap and let dirty-rect tracking redraw ~24x24 for a blink. With Phase 1
measuring a marginal 19.7 fps end-to-end, this was not a close call.

**No PNG art shipped.** The hybrid exists because LVGL 8.3 cannot draw
concave shapes. The Baby has `ear_style NONE` and `tail_style NONE`, so it
needs none. The seam (`pet_overlay`, `assets.h`, `convert_assets.py`) is wired
and empty rather than filled with placeholder art nobody intends to keep.

**Everything is form-driven.** Nothing about the Baby is hardcoded, so later
forms are a table row rather than a new renderer. All 6 eye styles and all 7
mouth styles are implemented, because they are form parameters — implementing
SLEEPY eyes is Phase 2; deciding the pet is asleep is Phase 5.

**Approximations, honestly marked.** LVGL 8.3 has no star, no spiral, and no
rotation for non-image objects. `EYE_STAR`, `EYE_SPIRAL`, `EYE_ANGRY_SLANT`
and `MOUTH_WOBBLE` are primitive approximations, marked `APPROX` in the source
and named as such in the serial output. They are the first genuine PNG
candidates when art exists.

## Verified on hardware

- Continuous breathing, random blinking (3–7 s), autonomous wandering,
  autonomous speech bubbles — all confirmed on the panel.
- Tap-to-react with a tier-1 bubble.
- All 6 eye styles, all 7 mouth styles, all 4 live-modifier states.
- No leak: LVGL heap flat at ~11.4 KB, high-water **6,159–6,377 bytes of
  48 KB**, fragmentation 1–2%, across the full sweep including screen changes.
- Phase 1 test card still correct, reachable with `D`.

### Bubble policy
Tier escalation confirmed on device: T3 accepted → T2 preempts → T1 preempts
→ T0 preempts → T0 then refuses all remaining requests. Typical stress result
**4 accepted / 16 suppressed of 20** — a low accept count is the correct
outcome, not a failure.

Cooldowns were verified **separately with spaced requests**, because firing 20
in one tick means "already showing" masks them. Global (`1963 ms left`), T2
(`9010 ms`) and T3 (`48001 ms`) all confirmed.

### Bubble layout — fixed after first test round
Three bugs, the third only visible after fixing the first two:

1. The box never got a size, keeping `lv_obj`'s ~100x50 default — the cause of
   both the oversized bubble for short text and the right-edge clipping.
2. Label width was hardcoded to 278 px, so nothing ever reached the wrap
   threshold.
3. **The clamp ran on stale geometry.** `LV_SIZE_CONTENT` + `lv_obj_update_layout()`
   still failed: LVGL refreshes content size lazily, so `lv_obj_get_width()`
   returned the *previous* message's width.

Fix: measure the text directly with `lv_txt_get_size()` — once unconstrained
for natural width, then again at the clamped width for real wrapped height —
and set the box size explicitly. The size is known *before* positioning, which
is the only way a same-frame clamp can be correct. `lv_obj_get_x()` is lazy
too, so the geometry report prints computed values, not queried ones.

Measured, 4 strings x 3 pet positions, all IN BOUNDS on a 368x448 panel:

| Text | Box w | h | Lines |
|---|---|---|---|
| `Hi!` | 68 | 50 | 1 |
| `HEY!` | 75 | 50 | 1 |
| `Is it snack time yet?` | 226 | 50 | 1 |
| `WHY AM I UPSIDE DOWN?!` | 260 (capped) | 72 | 2 |

Clamping genuinely exercised at both edges: at the left the 226-wide bubble
would centre at −17 and is pushed to 8; at the right the 260-wide would end at
402 and is pushed back to 100 (368 − 8 − 260).

### Idle wander cadence
8–20 s read as too infrequent on the real panel and was tuned to **4–9 s**.
The interval is measured from the END of the previous walk — the completion
path re-arms the timer on entry to IDLE — which is what prevents walks
chaining back-to-back at the shorter interval.

## Cost

| | Phase 1 | Phase 2 | Delta |
|---|---|---|---|
| Flash | 664,812 | 681,136 | +16,324 |
| RAM | 75,600 | 75,952 | +352 |
| LVGL high-water | 3,364 | ~6,300 | +~2,900 |

`LV_MEM_SIZE` is 48 KB against a ~6.3 KB high-water. Do **not** tune it down
yet — Phase 3 adds the menu and pages, which will be the real consumer.

## Open items

- End-to-end fps now measures the pet screen, not the Phase 1 card, so 19.7 is
  not directly comparable to Phase 1's 19.0. Both are below the ~20 the brief
  set for the page-slide animation — **Phase 3 should plan the cross-fade
  fallback.**
- PSRAM still shows 4,156 bytes against a v1 design target of 0. Uninvestigated.
- `BUBBLE_TEXT_MAX_W` is 232 px. Sentences like "Is it snack time yet?" fit on
  one line at 198 px and so never wrap. Lower it to ~170 if more wrapping is
  wanted; left as-is because forcing short sentences to wrap costs vertical
  space for no legibility gain.
