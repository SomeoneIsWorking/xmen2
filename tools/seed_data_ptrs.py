#!/usr/bin/env python3
"""Seed functions from code-shaped DWORDS in a /FIXED image's data.

`seed_relocs.py` is the right tool and it is an ENUMERATION: every absolute
address in a relocatable image has a base-relocation entry, so the set of
relocation values landing in an executable section is exactly "every absolute
code pointer in this module". It refuses on an image with no relocation
directory, and says so.

XMen2.exe is that image. It is linked /FIXED, so there is no table -- and the
discovery loop paid for it: with every DLL bulk-seeded from .reloc and
converging in one round, the exe still produced exactly ONE new indirect-call
target per round (0x0045fda0, then 0x004a93e0, ...), each costing a Ghidra
re-analysis, a re-emit and a relink.

The pointers are still THERE; only the index to them is missing. So this scans
the initialised, NON-executable sections for 4-byte-aligned dwords whose value
lands in an executable section.

    tools/seed_data_ptrs.py <module>.json [-o seeds.txt] [--pe <path>]

THIS IS A HEURISTIC, and seed_relocs.py is not -- the difference matters:

  * a 32-bit integer constant can have the same value as a code address. In
    this image the executable range is about 2.4 MB starting at 0x00401000,
    so any stored constant in [4198400, 6700000] is indistinguishable from a
    function pointer BY VALUE. The counts below say how many candidates were
    produced; AddFunctions.py, where the code/data separation actually lives,
    then rejects the ones that are defined data or do not disassemble, and
    says how many it rejected and why.
  * an address INSIDE a known function is a SPLIT, not a seed. Those are
    counted and listed separately -- silently seeding them is what made the
    loop spin (C132).

WHAT IT CANNOT SEE, so a clean run is not mistaken for a complete one:

  * an UNALIGNED pointer. Compilers align pointers in structures and tables;
    a packed byte stream holding one is invisible here.
  * a target computed at run time (base + index, an RVA table, a switch's own
    jump table) -- there is no stored address at all.
  * a pointer stored only in the CODE stream as an immediate. That is
    `seed_code_imms.py`, and the two are complementary.
  * a pointer that is only ever built in .text itself: executable sections are
    NOT scanned here, deliberately, because a dword-aligned window over
    instruction bytes produces a flood of coincidences.

REFUSES, rather than reporting nothing, when the image HAS a relocation
directory: then seed_relocs.py is both complete and free of the guesswork
above, and running this instead would trade an enumeration for a heuristic.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from seed_relocs import PE, load_json          # noqa: E402  (same PE reader)

IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_MEM_EXECUTE = 0x20000000


def data_pointer_candidates(pe):
    """Every 4-aligned dword in an initialised non-executable section whose
    value lands in an executable section. Returns (values, nscanned_bytes,
    [section names scanned])."""
    execs = pe.exec_ranges()
    lo = min(a for a, _, _ in execs)
    hi = max(b for _, b, _ in execs)
    vals = set()
    scanned = 0
    names = []
    for name, va, vsz, raw, rsz, ch in pe.sections:
        if ch & IMAGE_SCN_MEM_EXECUTE:
            continue
        if ch & IMAGE_SCN_CNT_UNINITIALIZED_DATA or not rsz:
            continue                      # .bss: no bytes in the file at all
        blob = pe.d[raw:raw + rsz]
        names.append(name)
        scanned += len(blob)
        # The section's own alignment in memory is 4 for every PE32 section
        # this project maps, so file offset and virtual address agree modulo 4.
        for o in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, o)[0]
            if lo <= v < hi and any(a <= v < b for a, b, _ in execs):
                vals.add(v)
    return vals, scanned, names


def main(argv):
    out = None
    pe_path = None
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "-o":
            out = argv[i + 1]
            i += 2
        elif argv[i] == "--pe":
            pe_path = argv[i + 1]
            i += 2
        elif argv[i] == "--selftest":
            return selftest()
        else:
            args.append(argv[i])
            i += 1
    if not args:
        sys.exit(__doc__)

    d = load_json(args[0])
    program = d.get("program") or os.path.basename(args[0])
    if pe_path is None:
        game = os.environ.get("GAME_PC_DIR")
        if not game:
            sys.exit("seed_data_ptrs: GAME_PC_DIR is not set and --pe was not "
                     "given, so the image to scan cannot be found. NOTHING "
                     "was searched.")
        pe_path = os.path.join(game, program)
    if not os.path.exists(pe_path):
        sys.exit("seed_data_ptrs: %s does not exist. NOTHING was searched."
                 % pe_path)

    pe = PE(pe_path)
    base = d.get("image_base")
    if isinstance(base, str):
        base = int(base, 16)
    if base is not None and base != pe.base:
        sys.exit("seed_data_ptrs: %s was exported at image base 0x%08x but the "
                 "shipped file is linked for 0x%08x. Seeding across that "
                 "mismatch would name addresses in the wrong image."
                 % (program, base, pe.base))

    vals = pe.reloc_values()[0]
    if vals is not None:
        sys.exit("seed_data_ptrs: %s HAS a relocation directory, so "
                 "seed_relocs.py enumerates its code pointers exactly and this "
                 "heuristic would only add guesses. Refusing; NOTHING was "
                 "searched." % program)

    cands, scanned, names = data_pointer_candidates(pe)
    execs = pe.exec_ranges()

    fns = d["functions"]
    eps = set()
    ranges = []
    for f in fns:
        ep = f["ep"]
        eps.add(ep)
        ranges.append((ep, ep + (f.get("size") or 1), f.get("name", "?")))
    ranges.sort()
    starts = [r[0] for r in ranges]

    def containing(a):
        import bisect
        i = bisect.bisect_right(starts, a) - 1
        return ranges[i] if i >= 0 and a < ranges[i][1] else None

    already = [a for a in sorted(cands) if a in eps]
    interior = {}
    fresh = []
    for a in sorted(cands):
        if a in eps:
            continue
        c = containing(a)
        if c:
            interior[a] = c[2]
        else:
            fresh.append(a)

    print("seed_data_ptrs: %s (%s)" % (program, pe_path))
    print("  scanned %d byte(s) of initialised non-executable data in section(s"
          ") %s" % (scanned, ", ".join(names) or "(NONE -- see below)"))
    if not names:
        sys.exit("seed_data_ptrs: this image has NO initialised non-executable "
                 "section, so this tool searched NOTHING. Refusing rather than "
                 "reporting an empty result.")
    print("  %d distinct dword(s) land in an executable section (%s)"
          % (len(cands),
             ", ".join("%s 0x%08x-0x%08x" % (n, lo, hi) for lo, hi, n in execs)))
    print("  of those: %d are already a function entry, %d fall INSIDE a known "
          "function (a SPLIT, not a seed)" % (len(already), len(interior)))
    for a, nm in sorted(interior.items())[:10]:
        print("      0x%08x inside %s" % (a, nm))
    if len(interior) > 10:
        print("      ... and %d more" % (len(interior) - 10))
    print("  CANDIDATE function starts: %d" % len(fresh))
    print("  Blind spots, always: unaligned pointers, addresses computed at "
          "run time, pointers that appear only as code immediates "
          "(seed_code_imms.py), and anything stored in .text.")
    print("  This is a HEURISTIC by value, not an enumeration: an integer "
          "constant in the executable range looks exactly like a function "
          "pointer here. AddFunctions.py rejects the ones that are defined "
          "data or do not disassemble, and reports what it rejected.")

    if out:
        with open(out, "w") as f:
            for a in fresh:
                f.write("0x%08x\n" % a)
        print("  wrote %d candidate(s) to %s" % (len(fresh), out))
    elif fresh:
        for a in fresh:
            print("0x%08x" % a)
    return 0


def selftest():
    """A crafted /FIXED PE32 with one code pointer in .rdata and one integer
    that is NOT in the executable range, so the test can fail in both
    directions: a scanner that found nothing, and one that took everything."""
    base = 0x00400000
    text_rva, text_raw, text_sz = 0x1000, 0x400, 0x200
    data_rva, data_raw, data_sz = 0x2000, 0x600, 0x200
    hdr = bytearray(0x400)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, 0x80)
    pe = 0x80
    hdr[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<H", hdr, pe + 4, 0x014C)          # i386
    struct.pack_into("<H", hdr, pe + 6, 2)               # 2 sections
    struct.pack_into("<H", hdr, pe + 20, 224)            # optional header size
    opt = pe + 24
    struct.pack_into("<H", hdr, opt, 0x10B)              # PE32
    struct.pack_into("<I", hdr, opt + 28, base)
    struct.pack_into("<I", hdr, opt + 92, 16)            # 16 data directories
    # every directory left zero -- in particular directory 5, .reloc
    sec = opt + 224
    for i, (nm, rva, vsz, raw, rsz, ch) in enumerate([
            (b".text", text_rva, text_sz, text_raw, text_sz, 0x60000020),
            (b".rdata", data_rva, data_sz, data_raw, data_sz, 0x40000040)]):
        s = sec + i * 40
        hdr[s:s + 8] = nm.ljust(8, b"\0")
        struct.pack_into("<IIII", hdr, s + 8, vsz, rva, rsz, raw)
        struct.pack_into("<I", hdr, s + 36, ch)

    img = bytearray(hdr) + bytearray(0x400)
    data = bytearray(data_sz)
    struct.pack_into("<I", data, 0, base + text_rva + 0x40)   # a code pointer
    struct.pack_into("<I", data, 4, 0x0000002A)               # a small integer
    struct.pack_into("<I", data, 8, base + 0x900000)          # outside the image
    img[data_raw:data_raw + data_sz] = data

    path = os.path.join(os.environ.get("TMPDIR", "."), "seed_data_ptrs_st.bin")
    with open(path, "wb") as f:
        f.write(img)
    try:
        p = PE(path)
        cands, scanned, names = data_pointer_candidates(p)
        want = base + text_rva + 0x40
        fails = 0
        if want not in cands:
            print("seed_data_ptrs selftest: FAILED -- the one real code "
                  "pointer (0x%08x) was not found among %d candidate(s); this "
                  "scanner could report a clean run having seen nothing."
                  % (want, len(cands)))
            fails += 1
        for bad in (0x0000002A, base + 0x900000):
            if bad in cands:
                print("seed_data_ptrs selftest: FAILED -- 0x%08x is not in an "
                      "executable section and was taken anyway." % bad)
                fails += 1
        if names != [".rdata"]:
            print("seed_data_ptrs selftest: FAILED -- scanned %r; .text must "
                  "NOT be scanned and .rdata must be." % (names,))
            fails += 1
        if scanned != data_sz:
            print("seed_data_ptrs selftest: FAILED -- scanned %d bytes, not "
                  "the %d in .rdata." % (scanned, data_sz))
            fails += 1
        print("seed_data_ptrs selftest: %s (%d candidate(s) from %d byte(s))"
              % ("FAILED" if fails else "PASSED -- the code pointer is found, "
                 "the integer and the out-of-image address are not, and .text "
                 "is not scanned", len(cands), scanned))
        return 1 if fails else 0
    finally:
        os.unlink(path)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
