#!/usr/bin/env python3
"""Read out ARK classes' vtables: address, length, and which slots are inherited.

    tools/ark_vtables.py <module>.json <module>.pe <module>.ark.json
                         [--class NAME] [--json out.json]

docs/RE/ark.md open question 4, and C009's stated falsifier, are the same thing:
handing libIGCore a vtable POINTER is not enough, because callers dispatch by
SLOT INDEX, and that layout had never been read out of the binary. This reads it.

## Where the address comes from

Every concrete class's `retrieveVTablePointer` builds a throwaway instance and
stamps its own vtable in before reading it back (C009). That store is a literal:

    MOV dword ptr [ESP + 0xc],0x100dd0a0     <-- the vtable of igDx8VisualContext

So the vtable address is recovered from the same function ARK already told us
about, with no symbol table involved.

## Where the END comes from -- and why it is not a guess

Walking dwords until one stops looking like a code pointer does not work: MSVC
emits vtables back to back, so that walk runs straight into the next class's
vtable and reports a single huge one. Because we recover EVERY concrete class's
vtable address, the sorted list gives each vtable's end as the next one's start.

Both bounds are computed anyway and **reported when they disagree**, because the
neighbour bound is only trustworthy where the neighbour is a class we know:
`code-scan` says where the pointers stop looking like code, `neighbour` says
where the next known vtable begins, and a gap between them means there is an
unregistered vtable in between that this cannot see. Silently taking the smaller
one would hide exactly that.

## Which slots are inherited

A derived class that does not override a slot leaves its parent's function
pointer in it, so the SAME address appears at the same index in many classes.
Counting how many classes share each slot's pointer recovers the inheritance
prefix without reading a single vtable-construction site: the leading slots that
almost every class shares are igObject's, and the first slot where a class
diverges from its siblings is where its own interface begins.
"""
import json
import re
import struct
import sys


class PE:
    def __init__(self, path):
        self.d = d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.secs, self.exec_ranges = [], []
        for i in range(nsec):
            o = pe + 24 + optsz + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode("latin1")
            vsz, va, rsz, ptr = struct.unpack_from("<IIII", d, o + 8)
            flags = struct.unpack_from("<I", d, o + 36)[0]
            self.secs.append((name, va, vsz, ptr, rsz))
            if flags & 0x20000000:
                self.exec_ranges.append((self.base + va, self.base + va + vsz))

    def dw(self, va):
        for _, s_va, vsz, ptr, rsz in self.secs:
            r = va - self.base - s_va
            if 0 <= r + 4 <= rsz:
                return struct.unpack_from("<I", self.d, ptr + r)[0]
        return None

    def is_code(self, va):
        return any(a <= va < b for a, b in self.exec_ranges)


# The vptr store inside retrieveVTablePointer: the first immediate written into
# the throwaway instance that points into this image.
VPTR_STORE = re.compile(r"^MOV (?:dword ptr )?\[ESP(?: \+ 0x[0-9a-fA-F]+)?\],"
                        r"0x([0-9a-fA-F]+)$")


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    jpath, pepath, arkpath = argv[0], argv[1], argv[2]
    only = argv[argv.index("--class") + 1] if "--class" in argv else None
    outp = argv[argv.index("--json") + 1] if "--json" in argv else None

    pe = PE(pepath)
    j = json.load(open(jpath))
    fn = {f["ep"]: f for f in j["functions"]}
    ark = json.load(open(arkpath))["classes"]

    concrete = [c for c in ark.values()
                if isinstance(c["retrieveVTablePointer"], int)
                and c["retrieveVTablePointer"]]
    if not concrete:
        sys.exit("ark_vtables: %s lists no class with a non-NULL "
                 "retrieveVTablePointer, so there is no vtable to read. "
                 "Refusing rather than printing an empty table." % arkpath)

    found, nofn, nostore = {}, [], []
    for c in concrete:
        f = fn.get(c["retrieveVTablePointer"])
        if f is None:
            nofn.append(c["name"])
            continue
        va = None
        for i in f["ins"]:
            m = VPTR_STORE.match(i["t"])
            if m:
                v = int(m.group(1), 16)
                if pe.dw(v) is not None and not pe.is_code(v):
                    va = v
                    break
        if va is None:
            nostore.append(c["name"])
        else:
            found[c["name"]] = va

    print("ark_vtables: %s" % j.get("program", jpath))
    print("  concrete classes: %d ; vtable address recovered for %d"
          % (len(concrete), len(found)))
    if nofn:
        print("  retrieveVTablePointer not decoded (no body): %d  %s"
              % (len(nofn), ", ".join(sorted(nofn)[:6])))
    if nostore:
        print("  decoded but NO vptr store matched: %d  %s"
              % (len(nostore), ", ".join(sorted(nostore)[:6])))
    if not found:
        sys.exit("ark_vtables: recovered 0 vtable addresses -- refusing")

    # ---- boundaries -------------------------------------------------------
    # Using only ARK-registered vtables as neighbours is not enough: abstract
    # classes emit a vtable too (with pure-virtual stubs) and ARK never names
    # its address, so those sit unseen between two registered ones and the
    # neighbour bound overshoots. Every vtable in the module, registered or
    # not, is pointed at by a vptr store somewhere in the code, so collect ALL
    # immediates that land on a run of function pointers.
    eps = set(fn)

    def is_fnptr(v):
        return v in eps

    IMM = re.compile(r"(?<![\w.])0x([0-9a-fA-F]{6,8})(?![\w.])")
    boundaries = set(found.values())
    for f in j["functions"]:
        for i in f.get("ins", []):
            for m in IMM.finditer(i["t"]):
                v = int(m.group(1), 16)
                if pe.dw(v) is not None and not pe.is_code(v) and \
                        is_fnptr(pe.dw(v)) and is_fnptr(pe.dw(v + 4) or 0):
                    boundaries.add(v)
    starts = sorted(boundaries)
    print("  vtable-start boundaries harvested from vptr stores across the "
          "whole module: %d (vs %d ARK-registered)" % (len(starts), len(found)))

    def bounds(va):
        nxt = next((s for s in starts if s > va), None)
        # A slot must point at a DECODED FUNCTION ENTRY, not merely into an
        # executable section: the looser test walks straight through the end of
        # the array into whatever data follows and inflates every count.
        n_scan = 0
        while True:
            v = pe.dw(va + n_scan * 4)
            if v is None or not is_fnptr(v):
                break
            n_scan += 1
        n_nb = (nxt - va) // 4 if nxt else None
        return n_scan, n_nb

    table = {}
    for name, va in sorted(found.items(), key=lambda kv: kv[1]):
        n_scan, n_nb = bounds(va)
        n = min(x for x in (n_scan, n_nb) if x is not None)
        table[name] = {"vtable": va, "slots": n,
                       "by_code_scan": n_scan, "by_neighbour": n_nb,
                       "fns": [pe.dw(va + k * 4) for k in range(n)]}

    dis = [(k, v) for k, v in table.items()
           if v["by_neighbour"] is not None
           and v["by_code_scan"] != v["by_neighbour"]]
    print("  vtables whose two independent length bounds DISAGREE: %d%s"
          % (len(dis), "" if not dis else
             "  (an unregistered vtable sits between them)"))
    for k, v in dis[:10]:
        print("      %-34s code-scan %d, neighbour %d"
              % (k, v["by_code_scan"], v["by_neighbour"]))

    # inheritance: how many classes share each slot's pointer
    print()
    if only:
        if only not in table:
            sys.exit("ark_vtables: %s has no recovered vtable (known: %s)"
                     % (only, ", ".join(sorted(table)[:10])))
        t = table[only]
        shared = {}
        for k in range(t["slots"]):
            p = t["fns"][k]
            shared[k] = sum(1 for o in table.values()
                            if k < o["slots"] and o["fns"][k] == p)
        print("  %s  vtable 0x%08x  %d slots  (instance size 0x%x)"
              % (only, t["vtable"], t["slots"], ark[only]["instanceSize"]))
        print("  slot  address     shared-with  (how many of the %d classes "
              "have the SAME pointer in this slot)" % len(table))
        for k in range(t["slots"]):
            s = shared[k]
            tag = "own" if s == 1 else ("inherited by %d" % s)
            print("   %3d   0x%08x  %s" % (k, t["fns"][k], tag))
    else:
        for name, t in sorted(table.items(), key=lambda kv: -kv[1]["slots"]):
            print("  %-36s vtable 0x%08x  %3d slots  size 0x%x"
                  % (name, t["vtable"], t["slots"], ark[name]["instanceSize"]))

    if outp:
        json.dump(table, open(outp, "w"), indent=1)
        print("\n  wrote %s" % outp)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
