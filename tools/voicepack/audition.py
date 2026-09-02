#!/usr/bin/env python3
"""
audition.py - decode clips OUT of a built pack, exactly as the device plays
them, and write WAVs plus objective measurements.

WHY DECODE FROM THE PACK rather than re-synthesise: what the child hears has
been through ADPCM, the 16 kHz device rate and the per-stage resample in
render_clip(). Auditioning the pre-encoded float audio would judge something
nobody ever hears. This mirrors render_clip(): advance = (pack_rate /
AU_RATE) * stage_pitch, linear interpolation, both of which are 16000 here so
the advance IS the stage pitch.

The measurements are objective only - duration, level, pace, fundamental.
Whether a voice sounds CUTE is not a number and is not decided here.
"""
import argparse, os, struct, sys, wave
import numpy as np

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
        73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
        408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,
        1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
        5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
        18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]

def adpcm_decode(data):
    """Mirrors adpcm_step_one() in audio.cpp."""
    pred, index, out = 0, 0, []
    for byte in data:
        for code in (byte & 0x0F, byte >> 4):       # low nibble first
            step = STEP[index]
            diffq = step >> 3
            if code & 4: diffq += step
            if code & 2: diffq += step >> 1
            if code & 1: diffq += step >> 2
            pred = pred - diffq if (code & 8) else pred + diffq
            pred = max(-32768, min(32767, pred))
            index = max(0, min(88, index + IDX[code]))
            out.append(pred)
    return np.array(out, dtype=np.int16)

def fnv1a(s):
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b; h = (h * 0x01000193) & 0xFFFFFFFF
    return h

# audio.cpp stage_pitch_of()
STAGE_PITCH = {"baby": 1.000, "kid": 0.980, "teen": 0.955, "adult": 0.930}

def load(path):
    d = open(path, "rb").read()
    n, rate, _ = struct.unpack("<III", d[4:16])
    idx = {}
    for i in range(n):
        h, off, ln = struct.unpack("<III", d[16+i*12:28+i*12])
        idx[h] = (off, ln)
    return d, idx, rate, 16 + n*12

def clip(path, text, stage):
    d, idx, rate, base = load(path)
    h = fnv1a(text)
    if h not in idx: return None, rate
    off, ln = idx[h]
    pcm = adpcm_decode(d[base+off: base+off+ln])
    adv = (rate / 16000.0) * STAGE_PITCH[stage]     # render_clip()
    n = int(len(pcm) / adv)
    i = np.arange(n) * adv
    i0 = i.astype(np.int32); i0 = np.clip(i0, 0, len(pcm)-2); frac = i - i0
    return (pcm[i0]*(1-frac) + pcm[i0+1]*frac).astype(np.int16), 16000

def f0(pcm, sr):
    """Autocorrelation fundamental over the loudest voiced window."""
    x = pcm.astype(np.float32)
    w = 1024
    if len(x) < w*2: return 0.0
    best = max(range(0, len(x)-w, w//2), key=lambda s: np.abs(x[s:s+w]).mean())
    seg = x[best:best+w] - x[best:best+w].mean()
    ac = np.correlate(seg, seg, "full")[w-1:]
    lo, hi = int(sr/400), int(sr/60)          # 60..400 Hz
    if hi >= len(ac): return 0.0
    return sr / (lo + int(np.argmax(ac[lo:hi])))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", action="append", required=True, help="name=path")
    ap.add_argument("--out", required=True)
    ap.add_argument("--line", action="append", required=True, help="label=text")
    ap.add_argument("--stages", default="baby,kid,teen,adult")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    packs = dict(p.split("=", 1) for p in a.pack)
    stages = a.stages.split(",")

    print(f"{'line':<22} {'voice':<6} {'stage':<6} {'sec':>5} {'rms':>6} "
          f"{'peak':>6} {'F0 Hz':>6} {'w/s':>5}")
    for spec in a.line:
        label, text = spec.split("=", 1)
        words = len(text.split())
        for vname, vpath in packs.items():
            for st in stages:
                pcm, sr = clip(vpath, text, st)
                if pcm is None:
                    print(f"{label:<22} {vname:<6} {st:<6}   MISSING"); continue
                dur = len(pcm)/sr
                rms = float(np.sqrt(np.mean(pcm.astype(np.float32)**2)))
                pk  = int(np.max(np.abs(pcm)))
                fn = f"{a.out}/{label}_{vname}_{st}.wav"
                with wave.open(fn, "wb") as w:
                    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
                    w.writeframes(pcm.tobytes())
                print(f"{label:<22} {vname:<6} {st:<6} {dur:5.2f} {rms:6.0f} "
                      f"{pk:6d} {f0(pcm,sr):6.0f} {words/dur:5.2f}")

if __name__ == "__main__":
    main()
