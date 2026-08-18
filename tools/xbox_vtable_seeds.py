#!/usr/bin/env python3
"""Harvest function entry points out of the XBE's vtables.

The runtime discovery loop (tools/xbox_discover.sh) finds ONE statically
invisible function per run: the game calls it, the call fails, the address
goes into xbox/seeds.json. That is correct but slow -- a virtual call reached
only after ten minutes of boot costs a full re-lift to discover.

Most of those addresses are not really invisible: they sit in VTABLES, as runs
of consecutive dwords pointing into the executable image. Nothing *references*
them (the compiler emitted a table, not a call), which is why the detector
misses them, but the table itself is the evidence.

This finds them in one pass. A candidate must clear three filters, because a
32-bit word that happens to look like a code address is common in data:

  1. It points inside the image's executable range.
  2. It is part of a RUN of at least MIN_RUN consecutive code pointers. One
     stray value is noise; three in a row is a table.
  3. It lands in an UNCLAIMED HOLE -- past the end of the nearest detected
     function and before the next one starts. An address INSIDE a detected
     function body is a mid-function label, not a missing function, and
     seeding it would create a bogus overlapping function.

Every filter's rejection count is printed. A run that finds nothing says how
many candidates it looked at and which filter removed them, so "no new
functions" is a measurement rather than an absence of output.

    tools/xbox_vtable_seeds.py            # report only
    tools/xbox_vtable_seeds.py --write    # merge into xbox/seeds.json
"""

import argparse
import bisect
import json
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_XBE = os.path.join(REPO, "vendor/xboxrecomp/game_files/default.xbe")
DEFAULT_FUNCS = os.path.join(
    REPO, "vendor/xboxrecomp/tools/disasm/output/functions.json")
DEFAULT_SEEDS = os.path.join(REPO, "xbox/seeds.json")

MIN_RUN = 3


def xbe_sections(data):
    """(name, virtual_addr, raw_size, raw_addr, executable) per XBE section."""
    base = struct.unpack_from("<I", data, 0x104)[0]
    nsec = struct.unpack_from("<I", data, 0x11C)[0]
    shdr = struct.unpack_from("<I", data, 0x120)[0] - base
    out = []
    for i in range(nsec):
        o = shdr + i * 0x38
        flags = struct.unpack_from("<I", data, o + 0x00)[0]
        vaddr = struct.unpack_from("<I", data, o + 0x04)[0]
        vsize = struct.unpack_from("<I", data, o + 0x08)[0]
        raw = struct.unpack_from("<I", data, o + 0x0C)[0]
        rsize = struct.unpack_from("<I", data, o + 0x10)[0]
        name_va = struct.unpack_from("<I", data, o + 0x14)[0] - base
        name = bytearray()
        while 0 <= name_va < len(data) and data[name_va]:
            name.append(data[name_va])
            name_va += 1
        out.append((name.decode("ascii", "replace"), vaddr, vsize,
                    raw, rsize, bool(flags & 4)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xbe", default=DEFAULT_XBE)
    ap.add_argument("--functions", default=DEFAULT_FUNCS)
    ap.add_argument("--seeds", default=DEFAULT_SEEDS)
    ap.add_argument("--min-run", type=int, default=MIN_RUN)
    ap.add_argument("--write", action="store_true",
                    help="merge the survivors into the seeds file")
    args = ap.parse_args()

    # Refuse rather than report an empty harvest: a missing input must not
    # look like "the binary has no vtables".
    for path in (args.xbe, args.functions):
        if not os.path.exists(path):
            print(f"xbox_vtable_seeds: required input missing: {path}\n"
                  f"  Nothing was scanned. Run tools/xbox_relift.sh first.",
                  file=sys.stderr)
            return 2

    data = open(args.xbe, "rb").read()
    secs = xbe_sections(data)

    db = json.load(open(args.functions))
    db = db["functions"] if isinstance(db, dict) and "functions" in db else db
    fns = []
    for f in (db.values() if isinstance(db, dict) else db):
        s, e = f.get("start"), f.get("end")
        s = int(s, 16) if isinstance(s, str) else s
        e = int(e, 16) if isinstance(e, str) else e
        if s and e:
            fns.append((s, e))
    fns.sort()
    starts = [s for s, _ in fns]
    startset = set(starts)

    # This XBE marks .rdata and .data executable (flags 0x06 / 0x07), so the
    # section flag cannot tell code from data here. Derive both from where the
    # disassembler actually found functions: the code range is their extent,
    # and a section holding none of them is a data section.
    if not fns:
        print("xbox_vtable_seeds: functions.json contains no functions -- "
              "nothing was scanned.", file=sys.stderr)
        return 2
    code_lo = fns[0][0]
    code_hi = max(e for _, e in fns)

    def has_functions(vaddr, vsize):
        i = bisect.bisect_left(starts, vaddr)
        return i < len(starts) and starts[i] < vaddr + vsize

    scanned = in_range = in_run = 0
    survivors = []
    seen = set()

    data_secs = [x for x in secs
                 if x[4] > 0 and not has_functions(x[1], x[2])]
    print("xbox_vtable_seeds: data sections (no detected function inside): "
          + ", ".join(n for n, *_ in data_secs))

    for name, _vaddr, _vsize, raw, rsize, _executable in data_secs:
        run = []
        for off in range(0, rsize - 4, 4):
            scanned += 1
            v = struct.unpack_from("<I", data, raw + off)[0]
            if code_lo <= v < code_hi:
                in_range += 1
                run.append(v)
                continue
            if len(run) >= args.min_run:
                in_run += len(run)
                for a in run:
                    if a in startset or a in seen:
                        continue
                    i = bisect.bisect_right(starts, a) - 1
                    if i >= 0 and a >= fns[i][1]:
                        seen.add(a)
                        survivors.append((a, name))
            run = []

    print(f"xbox_vtable_seeds: image code range 0x{code_lo:08X}-0x{code_hi:08X},"
          f" {len(fns)} detected functions")
    print(f"  {scanned} data words scanned")
    print(f"  {in_range} point into executable memory")
    print(f"  {in_run} are inside a run of >= {args.min_run} consecutive pointers"
          f" (vtable-shaped)")
    print(f"  {len(survivors)} of those are NEW and land in an unclaimed hole")
    print(f"  rejected: {scanned - in_range} not code-shaped,"
          f" {in_range - in_run} isolated (not part of a table),"
          f" {in_run - len(survivors)} already detected or mid-function")

    if not survivors:
        print("  -> nothing to add. This is a measured zero, not a failed scan.")
        return 0

    if not args.write:
        print("  -> report only; pass --write to merge into", args.seeds)
        return 0

    seeds = json.load(open(args.seeds)) if os.path.exists(args.seeds) else []
    have = {e["start"].lower() for e in seeds}
    added = 0
    for a, sec in sorted(survivors):
        key = f"0x{a:08X}"
        if key.lower() in have:
            continue
        seeds.append({
            "start": key,
            "name": f"vtable_{a:08X}",
            # Harvested candidates are inferred, not observed. A few will be
            # data that looked like a table; the relift check treats a
            # harvested seed that does not land as a note, and a
            # runtime-observed one that does not land as an error.
            "source": "harvest",
            "why": f"Entry in a vtable-shaped run of >= {args.min_run} "
                   f"consecutive code pointers in the {sec} section, landing "
                   f"in an unclaimed hole. Harvested by "
                   f"tools/xbox_vtable_seeds.py -- nothing references it, so "
                   f"the detector could not find it, but the table is the "
                   f"evidence that it is a function."})
        added += 1
    open(args.seeds, "w").write(json.dumps(seeds, indent=2) + "\n")
    print(f"  -> added {added} seeds to {args.seeds} ({len(seeds)} total)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
