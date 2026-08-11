#!/usr/bin/env python3
"""Seed functions from the module's own RELOCATION TABLE.

The discovery loop exists because indirect-call targets are invisible to
reference-driven analysis: nothing in the Ghidra database points at them, so
the first thing that knows they are code is the running game, which stops at
one of them. Each round then costs a Ghidra re-analysis, a re-emit and a
relink -- for ONE function. msdia80 gave exactly one per round for eight
rounds and was still going.

The two existing bulk seeders both guess, and both guess conservatively:

  * `SeedPointerTables.py` (Ghidra) wants THREE consecutive aligned dwords in
    read-only data, because that is what a vtable looks like and what a stray
    constant does not. A lone function pointer in a struct is invisible to it,
    and so is anything in .data unless SEED_SCAN_DATA is set.
  * `seed_code_imms.py` reads immediates out of instruction text, which finds
    callbacks passed to a registrar and nothing that lives in data.

The linker already wrote down the answer. Every absolute address baked into a
relocatable image has a BASE RELOCATION entry, because the loader has to fix it
up if the image moves -- so the set of relocation targets whose VALUE lands in
an executable section is a complete, mechanical enumeration of "every absolute
code pointer in this module". No run-length threshold, no alignment
assumption, no .rdata/.data distinction.

    tools/seed_relocs.py <module>.json [-o seeds.txt] [--pe <path>]

MEASURED (2026-08-11): both targets the runtime loop found in msdia80 one round
at a time were already sitting in .reloc. libIGCore has 51 code pointers that
are not functions yet, libIGOpt 5.

WHAT THIS CANNOT SEE, so that a clean run is not mistaken for a complete one:

  * a module with no relocation directory -- an EXE linked /FIXED. This
    REFUSES rather than reporting nothing found, because "searched nothing"
    and "found nothing" must not look alike.
  * a target computed at run time (base + index, an RVA table, a switch's own
    jump table) -- there is no absolute address in the file to relocate, so
    the linker never recorded one.
  * a pointer into a module OTHER than this one.

WHAT IT WILL NOT DO:

  * seed an address that is already a function entry (nothing to do);
  * seed an address INSIDE a known function -- that needs a SPLIT, and
    silently seeding one is what made the loop spin (C132). They are counted
    and listed separately.

A relocation value that lands in an executable section is not PROOF of code:
MSVC puts read-only tables and string literals in .text for a DLL with no
.rdata section, and a pointer to one of those is an ordinary data pointer.
Those are rejected on the Ghidra side, where the code/data separation lives --
`AddFunctions.py` refuses an address that is defined data or that does not
disassemble, and counts what it refused. This tool's job is to enumerate the
candidates completely; deciding which are code is Ghidra's.
"""
import json
import os
import struct
import sys

IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_HIGHLOW = 3


class PE(object):
    """Just enough PE32 to read sections and the base relocation table."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        d = self.d
        if d[:2] != b"MZ":
            raise ValueError("%s has no MZ signature" % path)
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise ValueError("%s has no PE header" % path)
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from("<H", d, opt)[0] != 0x10B:
            raise ValueError("%s is not PE32" % path)
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        ndir = struct.unpack_from("<I", d, opt + 92)[0]
        self.dirs = [struct.unpack_from("<II", d, opt + 96 + i * 8)
                     for i in range(ndir)]
        self.sections = []
        for i in range(nsec):
            s = pe + 24 + optsz + i * 40
            name = d[s:s + 8].rstrip(b"\0").decode("latin1")
            vsz, va, rsz, raw = struct.unpack_from("<IIII", d, s + 8)
            chars = struct.unpack_from("<I", d, s + 36)[0]
            self.sections.append((name, va, vsz, raw, rsz, chars))

    def exec_ranges(self):
        return [(self.base + va, self.base + va + vsz, name)
                for name, va, vsz, raw, rsz, ch in self.sections
                if ch & 0x20000000]

    def off(self, rva):
        for name, va, vsz, raw, rsz, ch in self.sections:
            if va <= rva < va + max(vsz, rsz):
                o = raw + (rva - va)
                return o if o < len(self.d) else None
        return None

    def reloc_values(self):
        """(values, nblocks, nhighlow, nother) -- the VALUE at every HIGHLOW
        site, as stored in the file (i.e. against the preferred base)."""
        if len(self.dirs) <= 5:
            return None, 0, 0, {}
        rva, size = self.dirs[5]
        if not rva or not size:
            return None, 0, 0, {}
        o = self.off(rva)
        if o is None:
            return None, 0, 0, {}
        end = o + size
        vals, nblk, nhl, other = [], 0, 0, {}
        while o + 8 <= end:
            prva, blk = struct.unpack_from("<II", self.d, o)
            if blk < 8:
                break
            nblk += 1
            for i in range(8, min(blk, end - o), 2):
                e = struct.unpack_from("<H", self.d, o + i)[0]
                typ, low = e >> 12, e & 0xFFF
                if typ == IMAGE_REL_BASED_ABSOLUTE:
                    continue          # padding, by definition
                if typ != IMAGE_REL_BASED_HIGHLOW:
                    other[typ] = other.get(typ, 0) + 1
                    continue
                fo = self.off(prva + low)
                if fo is None or fo + 4 > len(self.d):
                    continue
                nhl += 1
                vals.append(struct.unpack_from("<I", self.d, fo)[0])
            o += blk
        return vals, nblk, nhl, other


def load_json(path):
    with open(path) as f:
        return json.load(f)


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
            sys.exit("seed_relocs: GAME_PC_DIR is not set and --pe was not "
                     "given, so the image whose relocations are wanted cannot "
                     "be found. NOTHING was searched.")
        pe_path = os.path.join(game, program)
    if not os.path.exists(pe_path):
        sys.exit("seed_relocs: %s does not exist. NOTHING was searched."
                 % pe_path)

    pe = PE(pe_path)
    base = d.get("image_base")
    if isinstance(base, str):
        base = int(base, 16)
    if base is not None and base != pe.base:
        sys.exit("seed_relocs: %s was exported at image base 0x%08x but the "
                 "shipped file is linked for 0x%08x. Seeding across that "
                 "mismatch would name addresses in the wrong image."
                 % (program, base, pe.base))

    vals, nblk, nhl, other = pe.reloc_values()
    if vals is None:
        sys.exit("seed_relocs: %s has NO relocation directory -- it is linked "
                 "/FIXED, so there is no table of absolute addresses to read "
                 "and this tool searched NOTHING. That is not the same as "
                 "finding nothing: use seed_code_imms.py and the runtime loop "
                 "for this module." % program)
    if nhl == 0:
        sys.exit("seed_relocs: %s has a relocation directory with 0 HIGHLOW "
                 "entries, which cannot be right for a 32-bit image. Refusing "
                 "rather than reporting an empty result." % program)

    execs = pe.exec_ranges()

    def is_exec(a):
        return any(lo <= a < hi for lo, hi, _ in execs)

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

    seen = set(vals)
    into_code = sorted(a for a in seen if is_exec(a))
    already = [a for a in into_code if a in eps]
    interior = {}
    fresh = []
    for a in into_code:
        if a in eps:
            continue
        c = containing(a)
        if c:
            interior[a] = c[2]
        else:
            fresh.append(a)

    print("seed_relocs: %s (%s)" % (program, pe_path))
    print("  %d relocation block(s), %d HIGHLOW entr(ies)%s"
          % (nblk, nhl,
             ("; UNHANDLED types " + ", ".join("%d x%d" % (t, n) for t, n
                                               in sorted(other.items())))
             if other else ""))
    print("  %d distinct absolute address(es); %d of them point into an "
          "executable section (%s)"
          % (len(seen), len(into_code),
             ", ".join("%s 0x%08x-0x%08x" % (n, lo, hi) for lo, hi, n in execs)))
    print("  of those: %d are already a function entry, %d fall INSIDE a "
          "known function (a SPLIT, not a seed)" % (len(already), len(interior)))
    for a, nm in sorted(interior.items())[:10]:
        print("      0x%08x inside %s" % (a, nm))
    if len(interior) > 10:
        print("      ... and %d more" % (len(interior) - 10))
    print("  CANDIDATE function starts: %d" % len(fresh))
    print("  Blind spots, always: a target computed at run time (base+index, "
          "an RVA table, a switch's own jump table) has no absolute address in "
          "the file and is NOT in this count.")
    if fresh:
        print("  Not all of these are code: a pointer to a read-only table or "
              "a string that the compiler placed in .text relocates exactly "
              "like a function pointer. AddFunctions.py rejects the ones that "
              "are defined data or do not disassemble, and says how many.")

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
    """Build a minimal PE32 in memory whose ONLY relocation points at a dword
    holding a .text address, and prove the parser reports exactly that one.

    A reloc parser that silently returns nothing is the failure this guards
    against: every caller of it would then read "no candidates" as "the module
    is fully covered".
    """
    import tempfile
    base = 0x10000000
    sec_va, sec_raw, sec_sz = 0x1000, 0x400, 0x200
    rel_va, rel_raw = 0x2000, 0x600
    hdr = bytearray(sec_raw)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, 0x80)
    pe = 0x80
    hdr[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", hdr, pe + 4,
                     0x014C, 2, 0, 0, 0, 0xE0, 0x2102)   # 2 sections
    opt = pe + 24
    struct.pack_into("<H", hdr, opt, 0x10B)
    struct.pack_into("<I", hdr, opt + 28, base)           # ImageBase
    struct.pack_into("<I", hdr, opt + 92, 16)             # NumberOfRvaAndSizes
    struct.pack_into("<II", hdr, opt + 96 + 5 * 8, rel_va, 16)   # .reloc dir
    s = opt + 0xE0
    hdr[s:s + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", hdr, s + 8, sec_sz, sec_va, sec_sz, sec_raw)
    struct.pack_into("<I", hdr, s + 36, 0x60000020)       # code + execute
    s += 40
    hdr[s:s + 8] = b".reloc\0\0"
    struct.pack_into("<IIII", hdr, s + 8, 0x200, rel_va, 0x200, rel_raw)
    struct.pack_into("<I", hdr, s + 36, 0x42000040)

    text = bytearray(sec_sz)
    # A code pointer at .text+0x100, pointing at .text+0x40.
    struct.pack_into("<I", text, 0x100, base + sec_va + 0x40)
    rel = bytearray(0x200)
    struct.pack_into("<IIHH", rel, 0, sec_va, 12,
                     (IMAGE_REL_BASED_HIGHLOW << 12) | 0x100,
                     (IMAGE_REL_BASED_ABSOLUTE << 12) | 0)

    blob = bytes(hdr) + bytes(text) + bytes(rel)
    fd, path = tempfile.mkstemp(suffix=".dll")
    os.write(fd, blob)
    os.close(fd)
    try:
        p = PE(path)
        vals, nblk, nhl, other = p.reloc_values()
        want = base + sec_va + 0x40
        ok = (vals == [want] and nblk == 1 and nhl == 1 and not other
              and p.exec_ranges() == [(base + sec_va,
                                       base + sec_va + sec_sz, ".text")])
        print("seed_relocs --selftest: parsed %r from a crafted image whose "
              "one relocation points at 0x%08x -- %s"
              % ([hex(v) for v in (vals or [])], want, "PASS" if ok else "FAIL"))
        if not ok:
            print("  blocks=%d highlow=%d other=%r exec=%r"
                  % (nblk, nhl, other, p.exec_ranges()))
        return 0 if ok else 1
    finally:
        os.unlink(path)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
