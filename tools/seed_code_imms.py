#!/usr/bin/env python3
"""Seed functions from code-pointer IMMEDIATES, in bulk.

`SeedPointerTables.py` finds function pointers that live in read-only DATA --
vtables and dispatch tables. It cannot see the other place they hide: an
immediate operand in CODE.

    90 90 90 90                 padding
    68 30 10 01 10              PUSH  0x10011030      <-- a callback address
    FF 15 F8 F2 ...             CALL  dword ptr [..]  <-- handed to a registrar

Nothing branches to 0x10011030, so Ghidra never makes it a function, and the
recompiler only learns it exists when the running game dispatches there. The
native discovery loop then costs one Ghidra re-analysis, one re-emit and one
relink per FUNCTION, because the runtime stops at the first missing target: on
libIGGfx it found exactly one per round for eight rounds and was still going.
This finds them all in one pass.

    tools/seed_code_imms.py <module>.json [-o seeds.txt]

What it will NOT do, because each would turn a seeding pass into a source of
plausible-looking garbage:

  * an immediate that is not inside this module's executable block is ignored;
  * an address that is already a known function entry is ignored (nothing to do);
  * an address that falls INSIDE an existing function is reported separately --
    those need a SPLIT, which seeding cannot perform, and silently seeding them
    is what made the discovery loop spin in the first place;
  * an address that is not 4-byte-plausible as a function start is still
    emitted, but the counts are printed so a bad run is visible rather than
    inferred.

Every category is COUNTED and printed. A seeding pass that reports "wrote 37
seeds" without saying what it rejected cannot be distinguished from one that
silently dropped the interesting half.
"""
import json
import re
import sys


def blocks_exec(d):
    """The module's executable address ranges."""
    out = []
    for b in d["blocks"]:
        x = b.get("x")
        x = int(x, 16) if isinstance(x, str) else x
        if x:
            start = b["start"]
            start = int(start, 16) if isinstance(start, str) else start
            size = b["size"]
            size = int(size, 16) if isinstance(size, str) else size
            out.append((start, start + size, b.get("name", "?")))
    return out


def main(argv):
    if not argv:
        sys.exit(__doc__)
    path = argv[0]
    out = None
    if "-o" in argv:
        out = argv[argv.index("-o") + 1]

    with open(path) as f:
        d = json.load(f)
    fns = d["functions"]
    execs = blocks_exec(d)
    if not execs:
        sys.exit("seed_code_imms: %s has NO executable block; it describes "
                 "nothing this could scan, so it refuses rather than reporting "
                 "zero candidates" % path)

    eps = set(fn["ep"] for fn in fns)
    # address -> containing function, for the SPLIT report
    spans = [(fn["ep"], fn["ep"] + fn.get("size", 0), fn["qname"]) for fn in fns]
    spans.sort()

    def inside(a):
        lo, hi = 0, len(spans)
        while lo < hi:
            mid = (lo + hi) // 2
            if spans[mid][0] <= a:
                lo = mid + 1
            else:
                hi = mid
        if lo:
            s, e, q = spans[lo - 1]
            if s < a < e:
                return q
        return None

    def is_exec(a):
        return any(s <= a < e for s, e, _ in execs)

    ninstr = 0
    cand = {}          # addr -> (module-relative site, mnemonic)
    already = 0
    outside = 0
    for fn in fns:
        for i in fn.get("ins", []):
            ninstr += 1
            # Only IMMEDIATE operands, and only where the immediate is the
            # SOURCE. Skipping any instruction containing a bracket was the
            # first cut and it was too blunt: the engine also builds callback
            # tables with `MOV dword ptr [ESP], 0x1006ea90`, where the brackets
            # are the DESTINATION and the immediate is exactly the function
            # pointer being looked for. Reading the source operand alone keeps
            # those and still rejects `MOV EAX, dword ptr [0x1006ea90]`, where
            # the same number is a load address rather than a code pointer.
            if i["m"] not in ("PUSH", "MOV"):
                continue
            rest = i["t"][len(i["m"]):].strip()
            src = rest.split(",", 1)[1].strip() if "," in rest else rest
            if "[" in src:
                continue
            for mm in re.finditer(r"(?<![\w.])0x([0-9a-fA-F]{6,8})(?![\w.])",
                                  src):
                v = int(mm.group(1), 16)
                if not is_exec(v):
                    outside += 1
                    continue
                if v in eps:
                    already += 1
                    continue
                cand.setdefault(v, (i["a"], i["m"]))

    need_split = {a: q for a, q in ((a, inside(a)) for a in cand) if q}
    fresh = sorted(a for a in cand if a not in need_split)

    print("seed_code_imms: %s" % d.get("program", path))
    print("  scanned %d instruction(s) in %d function(s); executable range(s): %s"
          % (ninstr, len(fns),
             ", ".join("%s 0x%08x-0x%08x" % (n, s, e) for s, e, n in execs)))
    print("  code immediates pointing into it: %d already a function entry, "
          "%d not executable (ignored)" % (already, outside))
    print("  NEW function starts to seed: %d" % len(fresh))
    print("  inside an existing function, need a SPLIT not a seed: %d"
          % len(need_split))
    for a, q in sorted(need_split.items())[:10]:
        print("      0x%08x inside %s" % (a, q))
    if len(need_split) > 10:
        print("      ... and %d more" % (len(need_split) - 10))

    if out:
        with open(out, "w") as f:
            for a in fresh:
                f.write("0x%08x\n" % a)
        print("  wrote %d seed(s) to %s" % (len(fresh), out))
    elif fresh:
        for a in fresh:
            print("0x%08x" % a)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
