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
- **levels are matched by RMS, not by peak.** This was peak normalisation,
  which does not make two lines sound equally loud: peak is set by the single
  sharpest transient, so a line with a hard consonant gets scaled down for
  one sample and plays quiet throughout. Matching RMS over the voiced audio
  and keeping the peak ceiling only as a limiter took the girl pack's
  line-to-line spread from wide to 2.9 dB.
- **hallucinated repeats are cut** — see `repair_pauses()`. Fed a very short
  token sequence this model's duration predictor becomes unstable and emits
  the word, a second of silence, and then a second copy. "Hi!" did it in all
  six of six identical runs; "Yum!" ran to 6.2 s. It is not the trailing
  space, not `noise_w`, not `length_scale` (it happens at 1.0), and the
  tokenisation is upstream's own and correct. 77 of 321 clips carried an
  internal silence over half a second.

  The repair counts the pause marks the TEXT asks for, allows that many,
  clamps each to 320 ms, and treats anything beyond as hallucination. The
  discriminator has to be punctuation, because a repeat and a real pause look
  identical in the audio: "Um... I had a little accident." earns exactly one
  internal pause, "Hi!" earns none. Cutting at the first long gap
  unconditionally — the obvious fix — truncates the accident line to "Um...".
  After the repair: 2 clips over 500 ms, worst case 450 ms.

## WHICH FILES ARE SCANNED — the thing that breaks

This has now gone wrong TWICE, in two different ways, and the collector is
built around both failures.

**First failure — the whitelist was too small.** `SPOKEN_SOURCES` lists the
files whose string POOLS are spoken. The first pack scanned `dialogue.cpp`
alone and shipped 258 of the ~294 real lines, so the most-heard bubbles of
all — "Again! Again!", "That tickles!", "I'm really hungry!" — lived in
`strings.cpp`, were never rendered, and fell back to chirps.

**Second failure — a pool is not the only place a line lives.** The
return-from-absence greetings, including "Um... I had a little accident.",
are `return` statements inside `sim_return_greeting()`; `main.cpp` hands the
RESULT to `ui_bubble_say()`. `sim.cpp` was not in `SPOKEN_SOURCES`, so its
strings were never read — and because the call site passes a VARIABLE rather
than a literal, the inline pass could not see them either. They fell through
both nets. The same shape hid the five evolution reactions (`evo_line()`),
and a regex that required the literal immediately after the first comma hid
two more behind a ternary:

    ui_bubble_say(BUBBLE_T2_MOOD,
                  (now / 1000) % 2 ? "Turn the light off!"
                                   : "It's too bright!");

So `collect_lines()` now runs THREE passes: string pools in the spoken
sources; every literal inside a speech call, paren-balanced so ternaries and
line breaks cannot hide one; and literals returned from the functions in
`SPEECH_RETURN_FUNCS`. That last one is an allowlist rather than "scan every
return", because most `return "..."` here is a name table — `pet_stage_name`,
`gamerec_name`, `rtc_health_name` — and "Tilt Maze" is not speech.

`diag.cpp` (console fixtures) and `setclock.cpp` (UI labels) stay excluded:
the Visitor can never say them.

## Verifying, in three places

**A generator cannot detect its own blind spot**, so the check does not live
in the generator:

    # 1. host-side, complete and deterministic - every manifest line,
    #    every pack, exact totals, and it fails on missing / collision /
    #    zero-length / stale entries
    python3 tools/voicepack/check_coverage.py \
        --manifest tools/voicepack/manifest.tsv \
        --pack boy=data/voice_boy.bin --pack girl=data/voice_girl.bin

    # 2. on-device, walks the pack INDEX and compares the two packs against
    #    each other, plus a named regression list of every line that once
    #    shipped missing
    TAB then I

    # 3. on-device, asks the RUNTIME what it can say (necessarily partial -
    #    a live Visitor only reaches its own trait pools)
    TAB then x

    # and TAB then A speaks the previously-broken lines out loud, because a
    # successful lookup is not proof of audio.

`--manifest` writes `manifest.tsv`: every fixed spoken line, its hash and
where it came from. It is committed; the packs are not.

## Auditioning

    python3 tools/voicepack/audition.py --pack boy=data/voice_boy.bin \
        --out /tmp/aud --line "accident=Um... I had a little accident."

Decodes back OUT of a built pack and applies the device's own per-stage
resample, so what you hear has been through ADPCM and 16 kHz exactly as the
child hears it. Prints duration, RMS, peak, F0 and words/sec.
