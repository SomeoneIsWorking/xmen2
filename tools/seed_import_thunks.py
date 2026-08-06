#!/usr/bin/env python3
"""Seed the import JUMP THUNKS a module keeps in its .text, in bulk.

MSVC emits a table of six-byte thunks for imported functions:

    ff 25 6c 3f 64 00      jmp dword ptr [0x00643f6c]

The exe calls an import by calling its thunk, and the thunk jumps through the
IAT. Ghidra does not always make these functions -- nothing in the code
"contains" them and they are reached indirectly -- so the recompiler has no body
for them and the running game discovers them one at a time. Observed: four
rounds of the native discovery loop, each finding one address six bytes after
the last, all inside one table.

    tools/seed_import_thunks.py <pe-file> <module.iat> [-o seeds.txt]

The .iat file is what `tools/pe.py iat <pe> > <module>.iat` produces, and
tools/add_module.sh already generates one for every module.

Precision comes from the IAT rather than from the byte pattern alone: `ff 25`
followed by four arbitrary bytes appears in ordinary code, but `ff 25` followed
by the address of a KNOWN IAT slot is a thunk and essentially nothing else. The
slot list is read from the module's own import directory, so this cannot drift
from the binary it is scanning.

Every rejected candidate is counted and printed, because a seeding pass that
reports a number without saying what it discarded cannot be told from one that
quietly found nothing.
"""
import struct
import sys


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode("latin1")
        vsz, va, rsz, ptr = struct.unpack_from("<IIII", d, o + 8)
        flags = struct.unpack_from("<I", d, o + 36)[0]
        out.append((name, va, vsz, ptr, rsz, flags))
    return base, out


def iat_slots(path):
    """Every IAT slot VA, from `tools/pe.py iat`.

    Deliberately NOT re-parsed here. The first version of this tool walked the
    import directory itself and got it wrong in a way that looked like an
    answer: it reported 450,370 IAT slots for libIGGfx and then found zero
    thunks -- a confident zero, from a parser that was reading past the
    descriptor array. pe.py's reader is the project's verified one (I003) and
    its output is already generated for every module, so this consumes that
    instead of competing with it.
    """
    slots = set()
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[0].startswith("0x"):
                slots.add(int(p[0], 16))
    return slots


def main(argv):
    if not argv:
        sys.exit(__doc__)
    if len(argv) < 2:
        sys.exit(__doc__)
    path, iatpath = argv[0], argv[1]
    out = argv[argv.index("-o") + 1] if "-o" in argv else None
    with open(path, "rb") as f:
        d = f.read()
    base, secs = sections(d)
    slots = iat_slots(iatpath)
    if not slots:
        sys.exit("seed_import_thunks: %s lists no IAT slots, so there is "
                 "nothing to match thunks against -- refusing rather than "
                 "reporting zero candidates" % iatpath)

    IMAGE_SCN_MEM_EXECUTE = 0x20000000
    found, bad_slot = [], 0
    scanned = 0
    for name, va, vsz, ptr, rsz, flags in secs:
        if not (flags & IMAGE_SCN_MEM_EXECUTE):
            continue
        blob = d[ptr:ptr + rsz]
        scanned += len(blob)
        i = blob.find(b"\xff\x25")
        while i != -1:
            if i + 6 <= len(blob):
                slot = struct.unpack_from("<I", blob, i + 2)[0]
                if slot in slots:
                    found.append(base + va + i)
                else:
                    bad_slot += 1
            i = blob.find(b"\xff\x25", i + 1)

    print("seed_import_thunks: %s" % path)
    print("  %d IAT slot(s) in the import directory; scanned %d executable byte(s)"
          % (len(slots), scanned))
    print("  ff 25 sequences whose operand is NOT an IAT slot (ordinary code, "
          "ignored): %d" % bad_slot)
    print("  import thunks found: %d" % len(found))
    if out:
        with open(out, "w") as f:
            for a in sorted(found):
                f.write("0x%08x\n" % a)
        print("  wrote %d seed(s) to %s" % (len(found), out))
    else:
        for a in sorted(found):
            print("0x%08x" % a)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
