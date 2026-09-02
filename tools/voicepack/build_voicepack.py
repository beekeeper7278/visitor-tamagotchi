#!/usr/bin/env python3
"""
build_voicepack.py - render the Visitor's spoken lines into one packed asset.

WHY A SINGLE PACKED FILE rather than one file per line: SPIFFS is flat and
its per-file overhead and open() cost are both real, and the audio task would
be paying them mid-sentence. One blob with a sorted hash index read into RAM
at boot (280 entries x 12 B = 3.4 KB) turns "say this line" into a seek and a
read.

LINES ARE KEYED BY A HASH OF THEIR TEXT, not by an id. That means no call
site changes and no table to keep in sync: dialogue.cpp adds a line, this
script picks it up, and the device finds it because the string hashes the
same on both sides. A line with no asset simply falls back to the chirp
voice, so a stale pack degrades instead of breaking.

usage: build_voicepack.py --voice Samantha [--rate 155] [--out data/voice.bin]
"""
import argparse, hashlib, os, re, struct, subprocess, sys, tempfile, wave

SRC = os.path.join(os.path.dirname(__file__), "..", "..", "src")

def fnv1a(s: str) -> int:
    """Must match voice_hash() in src/voice.cpp exactly."""
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h

# Files whose string pools are SPOKEN by the Visitor. A whitelist rather
# than "every .cpp", because two files hold pools that are not speech:
# diag.cpp (console test fixtures) and setclock.cpp (on-screen UI labels).
# Rendering those would waste flash on lines the Visitor can never say.
#
# THIS LIST IS THE THING THAT BREAKS. The first pack scanned dialogue.cpp
# alone and shipped 258 of the ~300 real lines - so the most-heard bubbles of
# all ("Again! Again!", "That tickles!", "I'm really hungry!") lived in
# strings.cpp, were never rendered, and fell back to chirps. It looked like a
# playback bug from the sofa. If a line beeps instead of speaking, check here
# FIRST: the pack almost certainly does not contain it.
SPOKEN_SOURCES = ["dialogue.cpp", "strings.cpp", "farewell.cpp", "games.cpp"]

# Files that hold string pools the Visitor can NEVER say. Everything else in
# src/ is scanned for speech CALL SITES regardless of whether it is listed
# above - see collect_lines().
NOT_SPEECH_FILES = {"diag.cpp", "setclock.cpp"}

# Functions whose RETURNED literal is spoken. This is an allowlist and not a
# blanket "scan every return", because most `return "..."` in this codebase
# is a name-lookup table - pet_stage_name(), gamerec_name(), ui_pet_*_name(),
# rtc_health_name(), the page titles - and rendering "Tilt Maze" or "CORRUPT
# -> safe reset" as speech would be nonsense.
#
# THIS IS WHERE THE ACCIDENT LINE WAS LOST. sim_return_greeting() returns
# "Um... I had a little accident." and six siblings; main.cpp then hands the
# RESULT to ui_bubble_say(). sim.cpp was not in SPOKEN_SOURCES so its pool
# was never read, and because the call site passes a variable rather than a
# literal, the inline pass could not see it either. It fell through both
# nets and chirped, which from the sofa looks like a playback bug.
SPEECH_RETURN_FUNCS = {
    "sim_return_greeting",   # sim.cpp - the return-from-absence greetings
    "evo_line",              # evolve.cpp - said at every evolution
    "dialogue_mischief",     # dialogue.cpp - its default arm returns a literal
}


def strip_comments(t):
    """Remove C comments but keep string literals intact.

    Without this a scan for speech-shaped text drowns in prose: this project
    comments heavily, and the comments are full of quoted example lines."""
    out, i, n = [], 0, len(t)
    while i < n:
        if t[i] == '"':
            j = i + 1
            while j < n and not (t[j] == '"' and t[j-1] != "\\"):
                j += 1
            out.append(t[i:j+1]); i = j + 1
        elif t.startswith("/*", i):
            j = t.find("*/", i + 2); i = (j + 2) if j >= 0 else n
        elif t.startswith("//", i):
            j = t.find("\n", i); i = j if j >= 0 else n
        else:
            out.append(t[i]); i += 1
    return "".join(out)


def _call_args(txt, name):
    r"""Every argument span of a name(...) call, paren-balanced.

    Balanced rather than regex because the old inline pass used
    `name\([^,]+,\s*"..."` - which requires the literal to sit immediately
    after the first comma. It therefore missed

        ui_bubble_say(BUBBLE_T2_MOOD,
                      (now / 1000) % 2 ? "Turn the light off!"
                                       : "It's too bright!");

    i.e. any ternary, any nested call, anything wrapped onto a second line.
    Two real spoken lines were lost to exactly that."""
    spans = []
    for m in re.finditer(re.escape(name) + r"\s*\(", txt):
        i, depth = m.end(), 1
        while i < len(txt) and depth:
            if txt[i] == '"':
                j = i + 1
                while j < len(txt) and not (txt[j] == '"' and txt[j-1] != "\\"):
                    j += 1
                i = j
            elif txt[i] == "(": depth += 1
            elif txt[i] == ")": depth -= 1
            i += 1
        spans.append((m.start(), txt[m.end():i]))
    return spans


def _func_body(txt, name):
    """Crude but sufficient: from `name(` to the matching closing brace."""
    m = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", txt)
    if not m: return ""
    i, depth = m.end(), 1
    while i < len(txt) and depth:
        if txt[i] == "{": depth += 1
        elif txt[i] == "}": depth -= 1
        i += 1
    return txt[m.end():i]


LITERAL = r'"((?:[^"\\]|\\.)*)"'

def collect_lines(with_provenance=False):
    """Every string the Visitor can actually SAY, and where it came from.

    THREE passes, because one is not enough and the project has now been
    bitten by each gap in turn:

      1. string POOLS in the spoken-source files
      2. every literal inside a speech CALL, paren-balanced, in any file that
         is not explicitly non-speech
      3. literals RETURNED from the allowlisted speech functions

    Anything with a format specifier or an escape is skipped - assembled at
    runtime, cannot be prerendered, and stays text with a chirp."""
    prov = {}
    def add(text, where):
        prov.setdefault(text, set()).add(where)

    # --- pass 1: the string pools -----------------------------------------
    for fn in SPOKEN_SOURCES:
        path = os.path.join(SRC, fn)
        if not os.path.exists(path):
            print(f"  !! {fn} missing - its lines will chirp")
            continue
        raw = open(path, encoding="utf-8", errors="replace").read()
        txt = strip_comments(raw)
        # Both spellings appear: `static const char *const NAME[]` and
        # `static const char *NAME[]`. Matching only one silently halves the
        # pack, which is how this went wrong the first time.
        for m in re.finditer(
                r"static const char \*(?:const\s+)?[A-Za-z0-9_]+\[\]\s*=\s*\{(.*?)\};",
                txt, re.S):
            for lit in re.findall(LITERAL, m.group(1)):
                add(lit, f"{fn} [pool]")
        # The dream table is a struct array, so it needs its own pass.
        for m in re.finditer(r"static const dream_t DREAMS\[\]\s*=\s*\{(.*?)\n\};",
                             txt, re.S):
            for lit in re.findall(LITERAL, m.group(1)):
                add(lit, f"{fn} [dreams]")

    # --- passes 2 and 3: call sites and allowlisted returns ---------------
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".cpp") or fn in NOT_SPEECH_FILES:
            continue
        raw = open(os.path.join(SRC, fn), encoding="utf-8", errors="replace").read()
        txt = strip_comments(raw)
        for call in ("ui_bubble_say", "ui_bubble_say_deferred",
                     "audio_say", "audio_say_as"):
            for _, span in _call_args(txt, call):
                for lit in re.findall(LITERAL, span):
                    add(lit, f"{fn} [{call}]")
        for func in SPEECH_RETURN_FUNCS:
            body = _func_body(txt, func)
            for m in re.finditer(r"return\s+" + LITERAL + r"\s*;", body):
                add(m.group(1), f"{fn} [{func}]")

    clean = {}
    for t, where in prov.items():
        if "%" in t or "\\" in t:       # runtime-assembled: stays text + chirp
            continue
        if not re.search(r"[A-Za-z]", t):
            continue
        if len(t) < 2 or len(t) > 120:
            continue
        clean[t] = sorted(where)
    if with_provenance:
        return clean
    return sorted(clean)

# --- IMA ADPCM (4-bit) ----------------------------------------------------
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
        73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
        408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,
        1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
        5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
        18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]

def adpcm_encode(samples):
    """Samples must be PYTHON ints, not numpy int16.

    `diff = s - pred` overflows silently in int16 and the encoder then chases
    a wrapped value for the rest of the clip, which sounds like crackle. The
    packer feeds it struct.unpack output (already ints) so it never saw this;
    the audition tool fed it a numpy array and did. Coerced here so the codec
    is correct for every caller rather than only the careful ones."""
    pred, index, out, buf, have = 0, 0, bytearray(), 0, False
    for s in samples:
        s = int(s)
        step = STEP[index]
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        tmp = step
        if diff >= step:   code |= 4; diff -= step
        step >>= 1
        if diff >= step:   code |= 2; diff -= step
        step >>= 1
        if diff >= step:   code |= 1
        # mirror the decoder exactly, or drift accumulates over a whole line
        diffq = tmp >> 3
        if code & 4: diffq += tmp
        if code & 2: diffq += tmp >> 1
        if code & 1: diffq += tmp >> 2
        pred = pred - diffq if (code & 8) else pred + diffq
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + IDX[code]))
        if have:
            out.append(buf | (code << 4)); have = False
        else:
            buf = code & 0x0F; have = True
    if have:
        out.append(buf)
    return bytes(out)

# ---------------------------------------------------------------------------
# THE VOICE SETTINGS, and why each number is what it is.
#
# These were chosen BY EAR over several rounds, not derived. They are written
# down here rather than passed on the command line so a re-render reproduces
# the approved voice exactly instead of whatever the last person typed.
#
#   PITCH 1.46  How young the Visitor sounds. Resampling shifts the FORMANTS
#               up with the pitch, which is the small-creature effect - a
#               shorter vocal tract - and is why more shift reads as younger
#               rather than merely higher. 1.46 was the top of a four-step
#               ladder auditioned against intelligibility.
#   PACE  1.15  15% slower than natural. Cuteness costs clarity, and this
#               buys it back. Pitch and pace are INDEPENDENT here: the clip is
#               synthesised at length_scale = PACE * PITCH and then resampled
#               up by PITCH, which divides the duration back out. Slowing down
#               therefore costs nothing in pitch.
#   NOISE 0.70  Piper's prosody variation. Above the 0.667 default for
#               liveliness, below the point where it wobbles.
#   NOISE_W     LEFT AT THE MODEL DEFAULT (0.8). An earlier note claimed this
#               was lowered per voice; it never was - synth() has no such
#               parameter - and the approved samples were rendered with the
#               default. The naturalness fix was the punctuation one below,
#               not this. Recorded because a plausible-sounding claim that is
#               not true is worse than no note at all.
PITCH   = 1.46
PACE    = 1.15
NOISE   = 0.70
DEV_HZ  = 16000

def resample(pcm, ratio, src_hz, dst_hz):
    """Pitch shift and rate conversion in one pass."""
    import numpy as np
    step = ratio * src_hz / dst_hz
    idx = np.arange(0, len(pcm) - 1, step)
    i0 = idx.astype(np.int32); frac = idx - i0
    return (pcm[i0] * (1 - frac) + pcm[i0 + 1] * frac).astype(np.int16)

# --- PAUSE REPAIR ---------------------------------------------------------
# TWO REAL DEFECTS, found by measuring the built pack rather than by ear.
#
# 1. SHORT LINES SAY THEMSELVES TWICE. Feed this VITS model a very short
#    token sequence - "Hi!" is 15 ids including BOS/PAD/EOS - and its
#    duration predictor becomes unstable: it emits the word, a second or more
#    of silence, and then a hallucinated second utterance. Measured over six
#    identical runs of "Hi!", every one did it. It is not stochastic bad luck
#    and retrying does not clear it; it is not the trailing space, not
#    noise_w, not length_scale (it happens at 1.0 too), and the tokenisation
#    is upstream's own and correct. 77 of 321 boy clips carried an internal
#    silence over half a second, and "Yum!" ran to 6.2 seconds.
#
# 2. "..." PAUSES RUN TOO LONG - up to 1.3 s mid-sentence, which reads as the
#    Visitor having lost its thread.
#
# THE DISCRIMINATOR IS PUNCTUATION, and it has to be, because the two cases
# look identical in the audio: both are "speech, silence, speech". What
# separates them is whether a pause was ASKED FOR. "Um... I had a little
# accident." earns exactly one internal pause; "Hi!" earns none. So count the
# internal pause marks in the text, allow that many, clamp each to something
# natural, and treat anything beyond as hallucination and cut it.
#
# Cutting at the FIRST long gap unconditionally would have been the obvious
# fix and would have truncated the accident line to "Um...".
MAX_PAUSE_MS   = 320      # a natural mid-sentence beat
GAP_MIN_MS     = 260      # silence longer than this is a "pause"
SIL_THRESH     = 900      # |sample| below this is silence at 16 kHz

def _internal_pauses(text):
    """How many pauses the TEXT actually asks for. Trailing punctuation ends
    the line and buys no pause, so it is stripped first."""
    import re
    body = re.sub(r"[\s.!?,;:]+$", "", text)
    return len(re.findall(r"\.\.\.|[.,;:!?]", body))

def repair_pauses(pcm, text):
    import numpy as np
    if len(pcm) == 0: return pcm
    B = int(0.02 * DEV_HZ)                       # 20 ms resolution
    n = len(pcm) // B
    if n < 3: return pcm
    loud = [bool(np.max(np.abs(pcm[i*B:(i+1)*B])) > SIL_THRESH) for i in range(n)]

    runs, i = [], 0                              # (is_loud, start, end) in buckets
    while i < n:
        j = i
        while j < n and loud[j] == loud[i]: j += 1
        runs.append((loud[i], i, j)); i = j

    allowed = _internal_pauses(text)
    out, seen_speech, used = [], False, 0
    gap_max = int(MAX_PAUSE_MS / 20)
    for is_loud, a, b in runs:
        if is_loud:
            out.append(pcm[a*B:b*B]); seen_speech = True
            continue
        if not seen_speech:
            continue                             # leading silence: drop
        if (b - a) * 20 < GAP_MIN_MS:
            out.append(pcm[a*B:b*B]); continue   # a normal short gap, keep it
        if used < allowed:                       # an ASKED-FOR pause: clamp it
            used += 1
            out.append(pcm[a*B:min(b, a + gap_max)*B])
            continue
        break                                    # unasked-for: the rest is hallucination
    if not out: return pcm
    res = np.concatenate(out)

    # A CUT IS A CLICK unless it is faded. Dropping the leading silence and
    # truncating the hallucination both land mid-waveform, which steps the
    # signal from zero to whatever the sample happened to be - measured as 2
    # "abrupt start" clips on the first pass of this repair. 4 ms is
    # inaudible as a fade and completely removes the step.
    f = int(0.004 * DEV_HZ)
    if len(res) > 2 * f:
        res = res.astype(np.float32)
        res[:f]  *= np.linspace(0.0, 1.0, f)
        res[-f:] *= np.linspace(1.0, 0.0, f)
        res = res.astype(np.int16)
    return res


def render(voice, text):
    """One line, at the approved voice settings, at the device sample rate."""
    import numpy as np
    pcm = voice.synth(text, length_scale=PACE * PITCH, noise_scale=NOISE)
    pcm = resample(pcm, PITCH, voice.rate, DEV_HZ)
    # Trim near-silence at the ends: it is dead air the child waits through,
    # and across 258 lines it is a lot of flash spent on nothing.
    thr = 350
    nz = np.nonzero(np.abs(pcm) > thr)[0]
    if len(nz):
        a = max(0, nz[0] - 200); b = min(len(pcm), nz[-1] + 400)
        pcm = pcm[a:b]
    # Remove hallucinated repeats and over-long pauses - see repair_pauses().
    pcm = repair_pauses(pcm, text)
    # --- LEVEL: match LOUDNESS, not peaks ------------------------------
    # This was `g = min(3.0, 26000 / peak)` - pure peak normalisation, which
    # does NOT make two lines sound equally loud. Peak is set by the single
    # sharpest transient in a clip, so a line with a hard "K" or "!" gets
    # scaled down for that one sample and plays quiet throughout, while a
    # soft even line is pushed right up. Switching between them is the
    # loudness jump you hear.
    #
    # RMS over the SPEECH (everything above the trim threshold) is a far
    # better proxy for perceived loudness, so that is what is matched. The
    # peak ceiling then survives as a limiter rather than as the target: it
    # only pulls a clip DOWN, and only when RMS matching would have clipped
    # it. TARGET_RMS 4200 of 32767 is about -18 dBFS, which is a normal
    # speech level with comfortable headroom for the transients.
    # PEAK_CEIL leaves headroom for ADPCM. The codec is lossy and its
    # decoder can OVERSHOOT the sample it was given, so a clip mastered right
    # up against full scale decodes clipped on the device even though the
    # source was clean - one clip did exactly that at 30000.
    TARGET_RMS, PEAK_CEIL, MAX_GAIN = 4200.0, 27500.0, 8.0
    voiced = pcm[np.abs(pcm) > thr].astype(np.float32)
    rms = float(np.sqrt(np.mean(voiced * voiced))) if len(voiced) else 0.0
    peak = int(np.max(np.abs(pcm))) or 1
    g = min(MAX_GAIN, TARGET_RMS / rms) if rms > 1.0 else 1.0
    if peak * g > PEAK_CEIL:          # limit, do not re-target
        g = PEAK_CEIL / peak
    return [int(v) for v in (pcm.astype(np.float32) * g).clip(-32768, 32767)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="path to a Piper .onnx")
    ap.add_argument("--out", required=True, help="output .bin")
    ap.add_argument("--list-only", action="store_true")
    ap.add_argument("--manifest", help="write the auditable line manifest as TSV")
    a = ap.parse_args()

    prov  = collect_lines(with_provenance=True)
    lines = sorted(prov)
    print(f"{len(lines)} spoken lines")

    # THE MANIFEST: every fixed spoken line and where it came from. Written
    # before any rendering so it is available even from --list-only, and so
    # the coverage checker can be run against a pack built by anyone.
    if a.manifest:
        os.makedirs(os.path.dirname(os.path.abspath(a.manifest)), exist_ok=True)
        with open(a.manifest, "w", encoding="utf-8") as f:
            f.write("hash\tsource\ttext\n")
            for t in lines:
                f.write(f"{fnv1a(t):08x}\t{';'.join(prov[t])}\t{t}\n")
        print(f"manifest -> {a.manifest}  ({len(lines)} lines)")

    if a.list_only:
        for t in lines: print(f"    {';'.join(prov[t]):<34} {t}")
        return

    from piper_say import PiperVoice
    voice = PiperVoice(a.model)
    print(f"model {os.path.basename(a.model)} @ {voice.rate} Hz -> "
          f"{DEV_HZ} Hz, pitch x{PITCH}, pace x{PACE}")
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)

    entries, blob, seen = [], bytearray(), {}
    for i, text in enumerate(lines, 1):
        h = fnv1a(text)
        if h in seen:
            print(f"  !! hash collision, skipping: {text!r} vs {seen[h]!r}")
            continue
        seen[h] = text
        enc = adpcm_encode(render(voice, text))
        entries.append((h, len(blob), len(enc)))
        blob += enc
        if i % 50 == 0 or i == len(lines):
            print(f"  {i}/{len(lines)}  {len(blob)/1024:.0f} KB")

    entries.sort()                       # device binary-searches the index
    hdr = bytearray(b"VVP1")
    hdr += struct.pack("<I", len(entries))
    hdr += struct.pack("<I", DEV_HZ)
    hdr += struct.pack("<I", 0)
    for h, off, ln in entries:
        hdr += struct.pack("<III", h, off, ln)
    data = bytes(hdr) + bytes(blob)
    open(a.out, "wb").write(data)
    print(f"\nwrote {a.out}")
    print(f"  {len(entries)} clips, index {len(hdr)} B, audio {len(blob)} B")
    print(f"  TOTAL {len(data)/1024/1024:.2f} MB")
    LIMIT = 11.88 * 1024 * 1024
    if len(data) > LIMIT / 2:
        print("  *** a single pack over half the partition - two must fit ***")

if __name__ == "__main__":
    main()
