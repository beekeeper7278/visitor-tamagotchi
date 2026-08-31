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

def collect_lines():
    """Every string the Visitor can actually SAY.

    Pool arrays from the whitelisted files, plus literals handed straight to
    ui_bubble_say()/_deferred() anywhere in the project. Anything with a
    format specifier or an escape is skipped - those are assembled at runtime
    and cannot be prerendered, which is exactly the case the brief allows to
    stay as text with a chirp."""
    out = set()
    for fn in SPOKEN_SOURCES:
        path = os.path.join(SRC, fn)
        if not os.path.exists(path):
            print(f"  !! {fn} missing - its lines will chirp")
            continue
        txt = open(path, encoding="utf-8", errors="replace").read()
        # Both spellings appear: `static const char *const NAME[]` and
        # `static const char *NAME[]`. Matching only one silently halves the
        # pack, which is how this went wrong the first time.
        for m in re.finditer(
                r"static const char \*(?:const\s+)?[A-Za-z0-9_]+\[\]\s*=\s*\{(.*?)\};",
                txt, re.S):
            for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
                out.add(lit)
        # The dream table is a struct array, so it needs its own pass.
        for m in re.finditer(r"static const dream_t DREAMS\[\]\s*=\s*\{(.*?)\n\};",
                             txt, re.S):
            for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
                out.add(lit)

    # Inline literals said anywhere - these are real spoken lines too.
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".cpp") or fn == "diag.cpp":
            continue
        txt = open(os.path.join(SRC, fn), encoding="utf-8", errors="replace").read()
        for lit in re.findall(
                r'ui_bubble_say(?:_deferred)?\s*\([^,]+,\s*"((?:[^"\\]|\\.)*)"', txt):
            out.add(lit)

    clean = set()
    for t in out:
        if "%" in t or "\\" in t:        # runtime-assembled: stays text + chirp
            continue
        if not re.search(r"[A-Za-z]", t):
            continue
        if len(t) < 2 or len(t) > 120:
            continue
        clean.add(t)
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
    peak = int(np.max(np.abs(pcm))) or 1
    g = min(3.0, 26000.0 / peak)
    return [int(v) for v in (pcm.astype(np.float32) * g).clip(-32768, 32767)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="path to a Piper .onnx")
    ap.add_argument("--out", required=True, help="output .bin")
    ap.add_argument("--list-only", action="store_true")
    a = ap.parse_args()

    lines = collect_lines()
    print(f"{len(lines)} spoken lines")
    if a.list_only:
        for t in lines: print("   ", t)
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
