#!/usr/bin/env python3
"""Minimal PE32 reader for the Alchemy DLL-swap work.

Exists because winedump can dump exports and imports but will not give section
ranges, and the section a symbol's RVA lands in is the only sound way to tell a
function export from a DATA export. Guessing that from the mangled name is a
heuristic; this is a measurement.

Subcommands:
  sections <pe>            section table
  exports  <pe>            ordinal, RVA, kind (CODE/DATA/FORWARD), name
  imports  <pe>            module -> imported symbol
  surface  <target.dll> <pe>...
                           union of symbols the given PEs import from target.dll
  proxydef <pe> <fwdname>  emit a .def forwarding every export to <fwdname>

Every subcommand reports its denominator and exits non-zero if the input is
missing or has no table of the requested kind, so an empty result can never be
confused with "I looked and there was nothing".
"""
import struct
import sys
import os

# ---------------------------------------------------------------- PE parsing


class PE:
    def __init__(self, path):
        if not os.path.isfile(path):
            sys.exit("pe.py: no such file: %s (searched NOTHING)" % path)
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:2] != b"MZ":
            sys.exit("pe.py: %s is not an MZ image" % path)
        pe_off = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe_off:pe_off + 4] != b"PE\0\0":
            sys.exit("pe.py: %s has no PE signature at e_lfanew=0x%x" % (path, pe_off))
        self.path = path
        coff = pe_off + 4
        (self.machine, self.nsections, _, _, _, opt_size,
         _) = struct.unpack_from("<HHIIIHH", d, coff)
        opt = coff + 20
        self.magic = struct.unpack_from("<H", d, opt)[0]
        if self.magic != 0x10B:
            sys.exit("pe.py: %s is PE32+ (magic 0x%x); this tool handles PE32 only"
                     % (path, self.magic))
        self.image_base = struct.unpack_from("<I", d, opt + 28)[0]
        nva = struct.unpack_from("<I", d, opt + 92)[0]
        self.dirs = [struct.unpack_from("<II", d, opt + 96 + 8 * i)
                     for i in range(nva)]
        sec = opt + opt_size
        self.sections = []
        for i in range(self.nsections):
            o = sec + 40 * i
            name = d[o:o + 8].rstrip(b"\0").decode("latin1")
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, o + 8)
            chars = struct.unpack_from("<I", d, o + 36)[0]
            self.sections.append(dict(name=name, vaddr=vaddr, vsize=vsize,
                                      raddr=raddr, rsize=rsize, chars=chars))

    def sec_of(self, rva):
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s
        return None

    def off(self, rva):
        s = self.sec_of(rva)
        if s is None:
            return None
        return s["raddr"] + (rva - s["vaddr"])

    def cstr(self, rva):
        o = self.off(rva)
        if o is None:
            return None
        e = self.data.index(b"\0", o)
        return self.data[o:e].decode("latin1")

    # IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE
    def is_code_rva(self, rva):
        s = self.sec_of(rva)
        return bool(s and (s["chars"] & 0x20000020))

    def exports(self):
        """[(ordinal, rva, kind, name)] — kind in CODE/DATA/FORWARD."""
        if len(self.dirs) < 1 or self.dirs[0][0] == 0:
            return None                      # no export directory at all
        d, edir = self.data, self.dirs[0][0]
        edir_size = self.dirs[0][1]
        o = self.off(edir)
        if o is None:
            sys.exit("pe.py: export dir RVA 0x%x maps to no section" % edir)
        base = struct.unpack_from("<I", d, o + 16)[0]
        nfunc, nname = struct.unpack_from("<II", d, o + 20)
        afunc, aname, aord = struct.unpack_from("<III", d, o + 28)
        by_ord = {}
        fo, no, oo = self.off(afunc), self.off(aname), self.off(aord)
        for i in range(nname):
            nrva = struct.unpack_from("<I", d, no + 4 * i)[0]
            idx = struct.unpack_from("<H", d, oo + 2 * i)[0]
            by_ord[idx] = self.cstr(nrva)
        out = []
        for i in range(nfunc):
            rva = struct.unpack_from("<I", d, fo + 4 * i)[0]
            if rva == 0:
                continue
            if edir <= rva < edir + edir_size:
                kind, target = "FORWARD", self.cstr(rva)
            else:
                kind = "CODE" if self.is_code_rva(rva) else "DATA"
                target = None
            out.append((base + i, rva, kind, by_ord.get(i), target))
        return out

    def iat(self):
        """{iat_va: (module, symbol)} -- the address each import thunk lives at.

        A `CALL dword ptr [0x100091c8]` in recompiled code is not an unresolvable
        indirect call: 0x100091c8 is an IAT slot and the callee is known here.
        """
        if len(self.dirs) < 2 or self.dirs[1][0] == 0:
            return None
        d = self.data
        o = self.off(self.dirs[1][0])
        out = {}
        while True:
            oft, _, _, nrva, first = struct.unpack_from("<IIIII", d, o)
            if nrva == 0:
                break
            mod = self.cstr(nrva)
            names = self.off(oft or first)
            slot = first
            while True:
                v = struct.unpack_from("<I", d, names)[0]
                if v == 0:
                    break
                if v & 0x80000000:
                    sym = "@%d" % (v & 0xFFFF)
                else:
                    sym = self.cstr(v + 2)
                out[self.image_base + slot] = (mod, sym)
                names += 4
                slot += 4
            o += 20
        return out

    def imports(self):
        """[(module, symbol_or_None, ordinal_or_None)]."""
        if len(self.dirs) < 2 or self.dirs[1][0] == 0:
            return None
        d = self.data
        o = self.off(self.dirs[1][0])
        res = []
        while True:
            oft, _, _, nrva, first = struct.unpack_from("<IIIII", d, o)
            if nrva == 0:
                break
            mod = self.cstr(nrva)
            thunk = oft or first
            t = self.off(thunk)
            while True:
                v = struct.unpack_from("<I", d, t)[0]
                if v == 0:
                    break
                if v & 0x80000000:
                    res.append((mod, None, v & 0xFFFF))
                else:
                    res.append((mod, self.cstr(v + 2), None))
                t += 4
            o += 20
        return res


# ------------------------------------------------------------------ commands


def cmd_sections(argv):
    pe = PE(argv[0])
    print("%-10s %-10s %-10s %-10s %s" % ("NAME", "VADDR", "VSIZE", "RAW", "FLAGS"))
    for s in pe.sections:
        print("%-10s 0x%08x 0x%08x 0x%08x %s0x%08x"
              % (s["name"], s["vaddr"], s["vsize"], s["raddr"],
                 "CODE " if s["chars"] & 0x20000020 else "     ", s["chars"]))
    print("-- %d sections in %s" % (len(pe.sections), pe.path))


def cmd_exports(argv):
    pe = PE(argv[0])
    exp = pe.exports()
    if exp is None:
        sys.exit("pe.py: %s has NO export directory -- nothing was scanned" % pe.path)
    for ordn, rva, kind, name, tgt in exp:
        print("%6d 0x%08x %-8s %s%s"
              % (ordn, rva, kind, name or "<noname>",
                 " -> " + tgt if tgt else ""))
    n = len(exp)
    kinds = {}
    for e in exp:
        kinds[e[2]] = kinds.get(e[2], 0) + 1
    print("-- %d exports in %s: %s" % (n, pe.path, kinds))


def cmd_imports(argv):
    pe = PE(argv[0])
    imp = pe.imports()
    if imp is None:
        sys.exit("pe.py: %s has NO import directory -- nothing was scanned" % pe.path)
    per = {}
    for mod, sym, ordn in imp:
        per.setdefault(mod, []).append(sym or ("@%d" % ordn))
    for mod in sorted(per):
        for s in per[mod]:
            print("%-24s %s" % (mod, s))
    print("-- %d imports from %d modules in %s"
          % (len(imp), len(per), pe.path))


def cmd_surface(argv):
    target, pes = argv[0].lower(), argv[1:]
    if not pes:
        sys.exit("pe.py surface: no PE files given -- scanned NOTHING")
    syms, scanned, hit = set(), 0, 0
    for p in pes:
        imp = PE(p).imports()
        scanned += 1
        if imp is None:
            print("!! %s has no import directory" % p, file=sys.stderr)
            continue
        n0 = len(syms)
        for mod, sym, ordn in imp:
            if mod.lower() == target:
                syms.add(sym or ("@%d" % ordn))
        if len(syms) > n0:
            hit += 1
    for s in sorted(syms):
        print(s)
    print("-- %d unique symbols imported from %s; scanned %d PE files, %d of "
          "which contributed. NOT VISIBLE to this scan: runtime "
          "LoadLibrary/GetProcAddress and delay-load tables."
          % (len(syms), target, scanned, hit), file=sys.stderr)


def cmd_proxydef(argv):
    pe = PE(argv[0])
    fwd = argv[1]
    if fwd.lower().endswith(".dll"):
        fwd = fwd[:-4]
    exp = pe.exports()
    if exp is None:
        sys.exit("pe.py: %s has NO export directory" % pe.path)
    # No comments in the emitted .def: GNU ld's built-in .def parser chokes on a
    # leading ';' line. Stats go to stderr so the file stays machine-clean.
    print("LIBRARY %s" % os.path.basename(pe.path))
    print("EXPORTS")
    noname = ndata = 0
    for ordn, rva, kind, name, tgt in exp:
        if name is None:
            noname += 1
            continue
        # No "@ordinal": GNU ld's .def grammar accepts an ordinal OR a
        # forwarder, never both. Safe here only because nothing in the game
        # imports libIG* by ordinal -- verify with `pe.py imports | grep '@'`
        # before reusing this on a DLL where that is not established.
        line = '  "%s" = "%s.%s"' % (name, fwd, name)
        if kind == "DATA":
            line += " DATA"
            ndata += 1
        print(line)
    print("-- %d of %d exports forwarded to %s (%d DATA, %d skipped as "
          "ordinal-only)" % (len(exp) - noname, len(exp), fwd, ndata, noname),
          file=sys.stderr)
    if noname:
        print("!! %d ordinal-only exports were NOT forwarded -- the proxy is "
              "INCOMPLETE" % noname, file=sys.stderr)


def cmd_iat(argv):
    pe = PE(argv[0])
    t = pe.iat()
    if t is None:
        sys.exit("pe.py: %s has NO import directory -- scanned NOTHING" % pe.path)
    for va in sorted(t):
        mod, sym = t[va]
        print("0x%08x %s %s" % (va, mod, sym))
    print("-- %d IAT slots in %s" % (len(t), pe.path), file=sys.stderr)


CMDS = dict(sections=cmd_sections, exports=cmd_exports, imports=cmd_imports,
            surface=cmd_surface, proxydef=cmd_proxydef, iat=cmd_iat)

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in CMDS:
        sys.exit(__doc__)
    CMDS[sys.argv[1]](sys.argv[2:])
