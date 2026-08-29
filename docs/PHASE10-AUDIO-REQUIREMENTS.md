# Audio and volume control — captured requirements

**STATUS: NOT IMPLEMENTED, AND NOT TO BE BUILT NOW.** Recorded 2026-08-28.
Owned by the final roadmap item. v1 ships silent by design.

Only one thing is asked of the current work: **reserve a UI hook for the
volume control.** Do not build the audio subsystem.

## Volume control

User-accessible, four settings:

- **Mute**
- **Low**
- **Medium**
- **High**

**Mute silences sound only.** All speech bubbles and every other piece of
visual feedback continue exactly as before. Mute must never be a way to end up
with a pet that cannot communicate — which matters because a parent may well
leave the device muted permanently.

**[OPEN]** which page hosts the control. Clock/Info (page 5) already holds
device-level settings such as Set Time and Start Over, so it is the natural
home, but that is not decided.

## Candidate sounds

footsteps · eating · cleaning and bathroom effects · happy/sad/reaction
chirps · game sounds · menu feedback · sleep/wake

## Sleep behaviour — the specific constraint

**Normal night-time sleep is quiet.** A Baby taking a *daytime nap* may make
one or two cute snoring sounds occasionally. There must be **no repetitive
all-night snoring.**

Worth stating plainly because it is the requirement most likely to be lost:
this device will sit in a child's bedroom overnight. A charming snore at 2 pm
is a serious problem at 2 am. Any snore implementation needs to know whether
it is night, which means it depends on the RTC and the sleep-window logic
rather than on the audio layer alone.

## Hardware status — audio is blocked, and not only by scheduling

`BSP_AUDIO_VERIFIED` is **0**. Unknown for this board:

- I2S BCLK / LRCK / DOUT / DIN GPIOs
- the MCLK GPIO, or confirmation that MCLK is generated internally
- the PA / amplifier enable line, often a TCA9554 bit on these boards

The ES8311 codec is present and answers at `0x18`, but none of the routing is
verified. Under the Phase 1 freeze these must be established on hardware
before any audio code is written — they are unverified values, not frozen
ones, so filling them in is expected work when the phase arrives.
