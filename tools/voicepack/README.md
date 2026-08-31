# voicepack — the Visitor's spoken lines

Renders every fixed dialogue line into a packed asset the device plays back.
**The packs are generated artifacts and are NOT in git** (`*.bin` is ignored);
they are ~4 MB each and fully reproducible from this script plus the two
models.

## Why recorded speech at all

The Visitor started with a procedural chirp voice — pitched tone bursts, then
a formant synth with jitter, glides and consonants. The second genuinely
sounds like a mouth, and it still cannot say *"I did a mess. It's art."*,
because formant babble contains no WORDS. That is inherent, not a tuning
failure. Understandable meant recorded.

The chirp voice is still in the firmware and still earns its place: it is the
fallback for lines assembled at runtime (the farewell note), for a board with
no pack flashed, and for the wordless moments.

## Voices

| Gender | Model | Dataset | Licence |
|---|---|---|---|
| boy  | `en_US-norman-medium`  | LibriVox, 15.5 h, 1200 epochs | **public domain** |
| girl | `en_US-kristin-medium` | LibriVox, 11.5 h, 2000 epochs | **public domain** |

Both from <https://brycebeattie.com/files/tts/> via the Piper voice repo.
Public-domain training data was a deliberate requirement: Apple's system
voices sound fine but could never ship.

Gender picks the pack. Gender is PRESENTATION ONLY — the same standing rule
the egg colour lives under — and a voice is presentation. It reaches no
accumulator, no form choice, no care rate, no discipline roll, no evolution.

## Setup

Piper needs a venv; `piper-tts` pulls onnxruntime. Python 3.14 works.

    python3 -m venv piperenv
    ./piperenv/bin/python -m pip install piper-tts
    brew install espeak-ng          # REQUIRED, see below

**piper-tts's own phonemizer is broken on macOS.** The arm64 wheel ships an
`espeakbridge.so` whose espeak data path is hardcoded to the CI machine that
built it (`/Users/runner/work/...`), and `espeakbridge.initialize()` ignores
its argument, so neither `ESPEAK_DATA_PATH` nor the correctly-bundled
directory has any effect. The model loads; only phonemization fails. So
`piper_say.py` uses Homebrew espeak-ng for phonemes, piper's OWN unmodified
`phonemes_to_ids` for the mapping, and onnxruntime for inference — replacing
only the broken step, so the id conventions stay upstream's.

## Building

    export PIPER_VENV_SITE=$PWD/piperenv/lib/python3.14/site-packages
    ./piperenv/bin/python tools/voicepack/build_voicepack.py \
        --model models/en_US-norman-medium.onnx  --out data/voice_boy.bin
    ./piperenv/bin/python tools/voicepack/build_voicepack.py \
        --model models/en_US-kristin-medium.onnx --out data/voice_girl.bin
    pio run -t uploadfs        # ~80 s; does NOT touch NVS, the pet is safe

About 30 s per voice. `--list-only` prints the lines without rendering.

## The settings, and why

Chosen by ear over several rounds and hardcoded so a re-render reproduces the
approved voice rather than whatever was last typed. See the constants block in
`build_voicepack.py` for the full reasoning; briefly:

- **pitch ×1.46** — resampling shifts formants up with pitch, which is the
  small-creature effect. Top of a four-step ladder auditioned against
  intelligibility.
- **pace ×1.15** — cuteness costs clarity; this buys it back. Pitch and pace
  are independent: the clip is synthesised at `length_scale = pace * pitch`
  and resampled up by `pitch`, which divides the duration back out.
- **noise 0.70** — livelier than the 0.667 default, below where it wobbles.
- **punctuation is preserved** — `.` `?` `,` `!` are real ids (10, 13, 8, 4)
  and the model was trained with them. Dropping them does not flatten the
  delivery, it makes the model invent pauses in the wrong places. That was
  reported as "weird pauses between some of the words".

## WHICH FILES ARE SCANNED — the thing that breaks

`SPOKEN_SOURCES` lists the files whose string pools are spoken. The first pack
scanned `dialogue.cpp` alone and shipped 258 of the ~294 real lines, so the
most-heard bubbles of all — "Again! Again!", "That tickles!", "I'm really
hungry!" — lived in `strings.cpp`, were never rendered, and fell back to
chirps. From the sofa that looks like a playback bug.

`diag.cpp` (console fixtures) and `setclock.cpp` (UI labels) are deliberately
excluded: the Visitor can never say them.

**A generator cannot detect its own blind spot**, so the device carries the
check instead: console `TAB` then `x` runs a coverage sweep that asks the
RUNTIME which lines the Visitor can say and then asks the pack whether each
has a clip. Run it after changing dialogue.
