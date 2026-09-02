#!/usr/bin/env python3
"""
check_coverage.py - does every manifest line resolve in BOTH packs?

DETERMINISTIC AND COMPLETE, which the on-device sweep cannot be. That sweep
asks a LIVE Visitor what it can say, and the dialogue selectors are flavoured
by trait and form - so it only ever reaches its own trait pools plus the
generic fallbacks (179 distinct against a 294-clip pack, per HANDOFF 2e).
This reads the pack files directly and checks all 321, every run.

It reports TOTALS, not "passed", and fails on any of:
  - a line missing from the boy pack
  - a line missing from the girl pack
  - a malformed header or a truncated index
  - two lines whose hashes collide (an ambiguous lookup)
  - a zero-length or absurdly short clip (a broken render)

usage: check_coverage.py --manifest m.tsv --pack boy=data/voice_boy.bin ...
"""
import argparse, struct, sys, os

def fnv1a(s: str) -> int:
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b; h = (h * 0x01000193) & 0xFFFFFFFF
    return h

# 4-bit ADPCM: a clip shorter than this cannot hold a word. Chosen well below
# the shortest real line ("Hi!") rather than near it, so this catches broken
# renders without ever arguing about a legitimately terse one.
MIN_CLIP_BYTES = 400

def load_pack(path):
    d = open(path, "rb").read()
    if len(d) < 16 or d[:4] != b"VVP1":
        raise ValueError(f"{path}: bad magic (not a VVP1 pack)")
    n, rate, _ = struct.unpack("<III", d[4:16])
    need = 16 + n * 12
    if len(d) < need:
        raise ValueError(f"{path}: index truncated, need {need} have {len(d)}")
    idx = {}
    dup = []
    for i in range(n):
        h, off, ln = struct.unpack("<III", d[16 + i*12 : 28 + i*12])
        if h in idx: dup.append(h)
        idx[h] = (off, ln)
    return {"path": path, "n": n, "rate": rate, "idx": idx,
            "dup": dup, "blob": len(d) - need, "size": len(d)}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--pack", action="append", required=True,
                    help="name=path, repeatable")
    a = ap.parse_args()

    lines = []
    with open(a.manifest, encoding="utf-8") as f:
        next(f)
        for row in f:
            h, src, text = row.rstrip("\n").split("\t", 2)
            lines.append((text, src, int(h, 16)))

    packs = {}
    for spec in a.pack:
        name, path = spec.split("=", 1)
        packs[name] = load_pack(path)

    print(f"manifest: {len(lines)} fixed spoken lines")
    for nm, p in packs.items():
        print(f"  pack {nm:<5} {p['n']:>4} clips  {p['rate']} Hz  "
              f"{p['size']/1024/1024:.2f} MB")
    print()

    fails = 0

    # 1. hash collisions inside the manifest itself: two DIFFERENT texts that
    #    would resolve to one clip. The device looks up by hash, so this is an
    #    ambiguous lookup and the wrong line would play.
    byhash = {}
    for text, src, h in lines: byhash.setdefault(h, []).append(text)
    coll = {h: t for h, t in byhash.items() if len(t) > 1}
    print(f"hash collisions in the manifest : {len(coll)}")
    for h, t in coll.items():
        print(f"   !! {h:08x} {t}"); fails += 1

    # 2 and 3. per-pack coverage and clip sanity
    for nm, p in packs.items():
        missing = [(t, s) for t, s, h in lines if h not in p["idx"]]
        short   = [(t, p["idx"][h][1]) for t, s, h in lines
                   if h in p["idx"] and p["idx"][h][1] < MIN_CLIP_BYTES]
        over    = [(t, p["idx"][h]) for t, s, h in lines
                   if h in p["idx"] and p["idx"][h][0] + p["idx"][h][1] > p["blob"]]
        covered = len(lines) - len(missing)
        pct = 100.0 * covered / len(lines) if lines else 0.0
        print(f"\npack {nm}:")
        print(f"   covered            {covered}/{len(lines)}  ({pct:.1f}%)")
        print(f"   MISSING            {len(missing)}")
        print(f"   duplicate index    {len(p['dup'])}")
        print(f"   zero/short clips   {len(short)}  (< {MIN_CLIP_BYTES} B)")
        print(f"   out-of-range clips {len(over)}")
        extra = len(p["idx"]) - covered
        print(f"   clips not in the manifest: {extra}  (stale pack entries)")
        for t, s in missing[:40]: print(f"      MISS  {s:<34} {t!r}")
        for t, n in short[:20]:   print(f"      SHORT {n:>5} B  {t!r}")
        fails += len(missing) + len(p["dup"]) + len(short) + len(over)

    print()
    if fails:
        print(f"FAIL - {fails} problem(s)"); return 1
    print("PASS - every manifest line resolves in every pack, "
          "no collisions, no broken clips")
    return 0

if __name__ == "__main__":
    sys.exit(main())
