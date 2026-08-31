#!/usr/bin/env python3
"""Static queries against the Xbox build: the XBE image and its disassembly.

The PC side has Ghidra (`tools/ghidra_scripts/`); the Xbox side had nothing but
one-off scratch scripts, so every question about `default.xbe` was re-derived by
hand. This is that tooling, in one place:

    read     dwords/bytes at a virtual address, resolved through the sections
    find     every 4-byte occurrence of a value, by VA and section
    vtable   a vtable's slots, with the function name each slot points at
    strtab   a table of pointers-to-strings (enum name tables)
    func     one function's disassembly, or its callers
    vslot    every register-based `call [reg + slot]` candidate, by argument
    chain    calls made ON the object a virtual accessor returned
    aftercall calls made ON the object a direct function call returned
    selftest prove each of the above can produce BOTH answers

Every subcommand states its denominator: how much was searched, not only what
was found, so "found nothing" and "never looked" cannot be confused. A missing
image or disassembly is a refusal (exit 2), never an empty result. Inputs that
are absent because the Xbox assets were never extracted exit 77 (SKIP).

    python3 tools/xbe_query.py find 0x0015F5B0
    python3 tools/xbe_query.py vtable 0x004A9D6C --count 20
    python3 tools/xbe_query.py vslot 0x10 --imm 8 --imm 9
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
XBE = Path(os.environ.get("XBOX_XBE", ROOT / "scratch/xbox_iso/default.xbe"))
DISASM = Path(os.environ.get(
    "XBOX_DISASM", ROOT / "vendor/xboxrecomp/tools/disasm/output"))

SKIP = 77
REFUSE = 2


class Missing(Exception):
    """An input the query needs is not on this machine."""


class Refused(Exception):
    """The query cannot be answered, and must not answer approximately."""


# --------------------------------------------------------------------------
# the image


@dataclass(frozen=True)
class Section:
    name: str
    va: int
    vsize: int
    raw: int
    rsize: int


class Image:
    def __init__(self, path: Path):
        if not path.exists():
            raise Missing(
                f"{path} does not exist -- extract the Xbox build first "
                f"(XBOX_ISO in .env); queried NOTHING")
        self.path = path
        self.data = path.read_bytes()
        if self.data[:4] != b"XBEH":
            raise Refused(f"{path} is not an XBE (magic {self.data[:4]!r})")
        self.base = struct.unpack_from("<I", self.data, 0x104)[0]
        nsec = struct.unpack_from("<I", self.data, 0x11C)[0]
        secs_va = struct.unpack_from("<I", self.data, 0x120)[0]
        off = secs_va - self.base
        self.sections: list[Section] = []
        for i in range(nsec):
            h = off + i * 0x38
            _flags, va, vsize, raw, rsize = struct.unpack_from("<IIIII", self.data, h)
            name_va = struct.unpack_from("<I", self.data, h + 0x14)[0]
            name = self.data[name_va - self.base:name_va - self.base + 16]
            self.sections.append(
                Section(name.split(b"\0")[0].decode("latin1"), va, vsize, raw, rsize))

    def section_of(self, va: int) -> Section | None:
        for s in self.sections:
            if s.va <= va < s.va + s.vsize:
                return s
        return None

    def read(self, va: int, n: int) -> tuple[Section, bytes]:
        s = self.section_of(va)
        if s is None:
            raise Refused(
                "VA %#010x lies in none of the %d XBE sections (%s)"
                % (va, len(self.sections), ", ".join(x.name for x in self.sections)))
        off = s.raw + (va - s.va)
        avail = s.raw + s.rsize - off
        if avail <= 0:
            raise Refused(
                "VA %#010x is in %s but past its %d raw bytes -- uninitialised "
                "data has no file contents to read" % (va, s.name, s.rsize))
        return s, self.data[off:off + min(n, avail)]

    def cstring(self, va: int, limit: int = 256) -> str | None:
        try:
            _s, buf = self.read(va, limit)
        except Refused:
            return None
        end = buf.find(b"\0")
        if end < 0:
            return None
        text = buf[:end]
        if not text or any(b < 0x20 or b > 0x7E for b in text):
            return None
        return text.decode("latin1")


# --------------------------------------------------------------------------
# the disassembly

FUNC_LINE = re.compile(r"^(sub_[0-9A-Fa-f]+):")
INSN_LINE = re.compile(r"^  0x([0-9A-Fa-f]{8})  \S+\s+(.*?)\s*$")
PUSH = re.compile(r"^push\s+(.*)$")
IMM = re.compile(r"^(0x[0-9a-f]+|-?\d+)$")
REG = re.compile(r"^e[a-z]{2}$")


def asm_path() -> Path:
    p = DISASM / "asm/text.asm"
    if not p.exists():
        raise Missing(
            f"{p} does not exist -- run tools/xbox_relift.py (its disasm stage "
            f"writes it); scanned NOTHING")
    return p


def functions_db() -> list[dict]:
    p = DISASM / "functions.json"
    if not p.exists():
        raise Missing(f"{p} does not exist -- run tools/xbox_relift.py; "
                      f"scanned NOTHING")
    return json.loads(p.read_text())


def iter_insns():
    """Yield (function-name, address, text) for every disassembled instruction."""
    cur = "(before the first function)"
    with asm_path().open(errors="replace") as fh:
        for line in fh:
            m = FUNC_LINE.match(line)
            if m:
                cur = m.group(1)
                continue
            m = INSN_LINE.match(line)
            if m:
                yield cur, int(m.group(1), 16), m.group(2)


# --------------------------------------------------------------------------
# subcommands


def cmd_read(args) -> int:
    img = Image(XBE)
    n = args.count * (1 if args.bytes else 4)
    sec, buf = img.read(args.va, n)
    print("VA %#010x is in section %s (%d bytes of raw data available)"
          % (args.va, sec.name, len(buf)))
    if args.bytes:
        print(" ".join("%02x" % b for b in buf))
        return 0
    for i in range(0, len(buf) - 3, 4):
        w = struct.unpack_from("<I", buf, i)[0]
        s = img.cstring(w)
        extra = "  %r" % s if s else ""
        print("  +0x%04x  0x%08X%s" % (i, w, extra))
    return 0


def cmd_find(args) -> int:
    img = Image(XBE)
    for val in args.values:
        needle = struct.pack("<I", val & 0xFFFFFFFF)
        searched = 0
        hits = []
        for s in img.sections:
            blob = img.data[s.raw:s.raw + s.rsize]
            searched += len(blob)
            pos = blob.find(needle)
            while pos != -1:
                hits.append((s.name, s.va + pos))
                pos = blob.find(needle, pos + 1)
        print("0x%08X: %d occurrence(s) in %d bytes across %d sections"
              % (val, len(hits), searched, len(img.sections)))
        for name, va in hits:
            print("    %-10s VA 0x%08X" % (name, va))
    return 0


def _func_names() -> dict[int, str]:
    return {int(f["start"], 16): f["name"] for f in functions_db()}


def cmd_vtable(args) -> int:
    img = Image(XBE)
    names = _func_names()
    sec, buf = img.read(args.va, args.count * 4)
    print("vtable at VA %#010x (section %s), %d slots"
          % (args.va, sec.name, len(buf) // 4))
    for i in range(0, len(buf) - 3, 4):
        w = struct.unpack_from("<I", buf, i)[0]
        who = names.get(w)
        if who is None:
            tsec = img.section_of(w)
            who = ("(no function starts there; VA is in %s)" % tsec.name
                   if tsec else "(not a mapped VA -- probably not a slot)")
        print("  +0x%02x  0x%08X  %s" % (i, w, who))
    return 0


def cmd_strtab(args) -> int:
    img = Image(XBE)
    sec, buf = img.read(args.va, args.count * 4)
    print("table at VA %#010x (section %s), %d entries"
          % (args.va, sec.name, len(buf) // 4))
    resolved = 0
    for i in range(0, len(buf) - 3, 4):
        w = struct.unpack_from("<I", buf, i)[0]
        s = img.cstring(w)
        if s is not None:
            resolved += 1
        print("  [%3d] +0x%04x  0x%08X  %s"
              % (i // 4, i, w, repr(s) if s is not None else "(not a string pointer)"))
    print("%d of %d entries resolved to a string" % (resolved, len(buf) // 4))
    return 0


def cmd_func(args) -> int:
    want = "sub_%08X" % args.addr
    if args.callers:
        db = functions_db()
        for f in db:
            if int(f["start"], 16) == args.addr:
                cb = f.get("called_by") or []
                print("%s: %d direct call site(s) recorded" % (f["name"], len(cb)))
                for c in cb:
                    print("   ", c)
                if not cb:
                    print("    (none -- reached only indirectly, or not reached "
                          "at all; `vslot` and `find` cover the indirect case)")
                return 0
        raise Refused("no function starts at 0x%08X among the %d in %s"
                      % (args.addr, len(db), DISASM / "functions.json"))
    header = "; Function: %s" % want
    out, inside = [], False
    with asm_path().open(errors="replace") as fh:
        for line in fh:
            if not inside:
                if line.rstrip("\n") == header:
                    inside = True
                    out.append(line.rstrip())
                continue
            out.append(line.rstrip())
            if line.startswith("; end of function"):
                break
    if not inside:
        raise Refused("%s is not present in %s -- no function was detected at "
                      "that address" % (want, asm_path()))
    print("\n".join(out))
    return 0


@dataclass(frozen=True)
class VslotSite:
    function: str
    address: int
    pushed: tuple[str, ...]
    window: tuple[tuple[int, str], ...]


def vslot_sites(slot: int, lookback: int) -> tuple[list[VslotSite], int]:
    # ESP-relative indirect calls address stack-held function pointers, not a
    # vtable.  The remaining sites are still class-agnostic candidates: the
    # same numeric slot can belong to unrelated classes.
    call_pat = re.compile(
        r"^call\s+dword ptr \[(?:eax|ebx|ecx|edx|esi|edi|ebp) \+ 0x%x\]$" % slot)
    window: list[tuple[int, str]] = []
    sites = []
    funcs = set()
    for fn, addr, text in iter_insns():
        if window and window[-1][0] > addr:
            window = []
        funcs.add(fn)
        if call_pat.match(text):
            pushed = []
            for _a, t in window:
                p = PUSH.match(t)
                if p:
                    pushed.append(p.group(1).strip())
            sites.append(VslotSite(fn, addr, tuple(pushed), tuple(window)))
        window.append((addr, text))
        if len(window) > lookback:
            window.pop(0)
    return sites, len(funcs)


def select_vslot_sites(sites: list[VslotSite], wanted: list[int]) -> list[VslotSite]:
    if not wanted:
        return sites
    return [site for site in sites
            if any(int(value, 0) in wanted
                   for value in site.pushed if IMM.match(value))]


def cmd_vslot(args) -> int:
    sites, nfuncs = vslot_sites(args.slot, args.window)
    kinds: Counter[str] = Counter()
    imms: Counter[int] = Counter()
    for site in sites:
        last = site.pushed[-1] if site.pushed else None
        if last is None:
            kinds["no push within %d instructions" % args.window] += 1
        elif IMM.match(last):
            kinds["immediate"] += 1
            imms[int(last, 0)] += 1
        elif REG.match(last):
            kinds["register (opaque to a literal scan)"] += 1
        else:
            kinds["memory/other (opaque to a literal scan)"] += 1
    # --show-sites controls detail, not selection.  When --imm is present it
    # remains the filter even if the matching sites are expanded.
    hits = select_vslot_sites(sites, args.imm) if (args.imm or args.show_sites) else []

    total = sum(kinds.values())
    print("scanned %d functions in %s" % (nfuncs, asm_path()))
    print("register-based [reg + 0x%x] candidates: %d" % (args.slot, total))
    print("  class identity is not inferred; unrelated vtables can share this slot")
    for k, n in kinds.most_common():
        print("  %-46s %4d  (%.1f%%)" % (k, n, 100.0 * n / total if total else 0.0))
    opaque = total - kinds["immediate"]
    print("a literal-immediate scan of this slot can see %d of %d sites; the "
          "other %d push a register or nothing" % (kinds["immediate"], total, opaque))
    if imms and args.histogram:
        print("immediate values seen (value: sites):")
        for v, n in sorted(imms.items()):
            print("    %-10s %d" % (hex(v), n))
    if args.imm:
        print("\nsites pushing a literal in %s: %d"
              % ([hex(v) for v in args.imm], len(hits)))
    elif args.show_sites:
        print("\nall call sites: %d" % len(hits))
    for site in hits:
        print("\n%s  0x%08X   pushes=%s"
              % (site.function, site.address, list(site.pushed)))
        for address, text in site.window:
            print("    0x%08X  %s" % (address, text))
    return 0


MOV_FROM_EAX = re.compile(r"^mov\s+(e[a-z]{2}), eax$")
LOAD_VTABLE = re.compile(r"^mov\s+(e[a-z]{2}), dword ptr \[(e[a-z]{2})\]$")
SET_THIS = re.compile(r"^mov\s+ecx, (e[a-z]{2})$")
VCALL_REG = re.compile(r"^call\s+dword ptr \[(e[a-z]{2}) \+ (0x[0-9a-f]+)\]$")


def chain_sites(via: int, window: int = 25):
    """Yield (slot, args, function, address) for every virtual call made on the
    object returned by a `call dword ptr [reg + <via>]`.

    The object is followed by register: the returned `eax`, whatever register it
    is moved into, and the vtable register loaded from it. Anything that breaks
    that chain ends the attribution, so a call on an unrelated object is not
    credited to the accessor -- the mistake this exists to avoid.
    """
    getter = re.compile(
        r"^call\s+dword ptr \[(?:eax|ebx|ecx|edx|esi|edi|ebp) \+ 0x%x\]$" % via)
    buf = list(iter_insns())
    ngetter = 0
    for i, (fn, _a, text) in enumerate(buf):
        if not getter.match(text):
            continue
        ngetter += 1
        holder, vreg, args = "eax", None, []
        for fn2, a2, t2 in buf[i + 1:i + 1 + window]:
            if fn2 != fn:
                break
            m = MOV_FROM_EAX.match(t2)
            if m:
                holder = m.group(1)
                continue
            m = LOAD_VTABLE.match(t2)
            if m and m.group(2) == holder:
                vreg = m.group(1)
                continue
            m = PUSH.match(t2)
            if m:
                args.append(m.group(1).strip())
                continue
            if SET_THIS.match(t2) and SET_THIS.match(t2).group(1) == holder:
                continue
            m = VCALL_REG.match(t2)
            if m:
                if vreg and m.group(1) == vreg:
                    yield int(m.group(2), 16), tuple(args), fn, a2
                    args = []
                    continue
                break
    yield None, ngetter, None, None      # trailer: the denominator


def cmd_chain(args) -> int:
    combos: Counter = Counter()
    examples = {}
    sites = []
    ngetter = 0
    seen_sites = set()
    for slot, a, fn, addr in chain_sites(args.via, args.window):
        if slot is None:
            ngetter = a
            continue
        site = (slot, a, fn, addr)
        if site in seen_sites:
            continue
        seen_sites.add(site)
        combos[(slot, a)] += 1
        examples.setdefault((slot, a), (fn, addr))
        sites.append(site)
    attributed = sum(combos.values())
    print("accessor slot +0x%02x: %d raw candidate(s); %d unique virtual "
          "call(s) on the returned object attributed within %d instructions"
          % (args.via, ngetter, attributed, args.window))
    if not attributed:
        print("  none -- either nothing is called on the result, or the "
              "register chain is broken sooner than this scan follows it")
    for (slot, a), n in sorted(combos.items()):
        if args.slot is not None and slot != args.slot:
            continue
        fn, addr = examples[(slot, a)]
        print("  slot +0x%02x  args=%-24s %3d  e.g. %s 0x%08X"
              % (slot, str(list(a)), n, fn, addr))
    if args.show_sites:
        selected = [site for site in sites
                    if args.slot is None or site[0] == args.slot]
        print("matching attributed call sites: %d" % len(selected))
        for slot, a, fn, addr in selected:
            print("  %s 0x%08X  slot +0x%02x  args=%s"
                  % (fn, addr, slot, list(a)))
    return 0


def returned_sites(callee: int, window: int = 25):
    """Yield virtual calls on the object returned by a direct call to `callee`.

    This is the class-preserving counterpart to `vslot`: it begins at a known
    singleton/accessor function and follows only that returned object, so an
    unrelated class with the same numeric vtable slot cannot become evidence.
    """
    # The disassembler prints instruction operands at a fixed eight hex
    # digits, while callers naturally spell query addresses with or without
    # their leading zeroes.  Compare the parsed address, not its rendering.
    direct = re.compile(r"^call\s+0x([0-9a-f]+)(?:\s+;.*)?$")
    buf = list(iter_insns())
    ncall = 0
    for i, (fn, _address, text) in enumerate(buf):
        match = direct.match(text)
        if not match or int(match.group(1), 16) != callee:
            continue
        ncall += 1
        holder, vreg, args = "eax", None, []
        for fn2, address2, text2 in buf[i + 1:i + 1 + window]:
            if fn2 != fn:
                break
            moved = MOV_FROM_EAX.match(text2)
            if moved:
                holder = moved.group(1)
                continue
            loaded = LOAD_VTABLE.match(text2)
            if loaded and loaded.group(2) == holder:
                vreg = loaded.group(1)
                continue
            pushed = PUSH.match(text2)
            if pushed:
                args.append(pushed.group(1).strip())
                continue
            set_this = SET_THIS.match(text2)
            if set_this and set_this.group(1) == holder:
                continue
            virtual = VCALL_REG.match(text2)
            if virtual:
                if vreg and virtual.group(1) == vreg:
                    yield int(virtual.group(2), 16), tuple(args), fn, address2
                break
    yield None, ncall, None, None


def cmd_aftercall(args) -> int:
    seen = set()
    sites = []
    direct_calls = 0
    for slot, pushed, fn, address in returned_sites(args.callee, args.window):
        if slot is None:
            direct_calls = pushed
            continue
        site = (slot, pushed, fn, address)
        if site not in seen:
            seen.add(site)
            sites.append(site)
    selected = [site for site in sites
                if args.slot is None or site[0] == args.slot]
    print("direct call 0x%08x: %d site(s); %d unique virtual call(s) "
          "attributed within %d instructions"
          % (args.callee, direct_calls, len(sites), args.window))
    print("matching attributed call sites: %d" % len(selected))
    counts = Counter((slot, pushed) for slot, pushed, _fn, _address in selected)
    for (slot, pushed), count in sorted(counts.items()):
        print("  slot +0x%03x args=%-28s %3d"
              % (slot, str(list(pushed)), count))
    if args.show_sites:
        for slot, pushed, fn, address in selected:
            print("  %s 0x%08X  slot +0x%03x  args=%s"
                  % (fn, address, slot, list(pushed)))
    return 0


# --------------------------------------------------------------------------
# selftest: every check below is run against a case that MUST come out positive
# and a case that MUST come out negative, so a check that can only ever agree
# with itself fails here rather than in a result someone believes.

# Measured facts, all re-derivable with the subcommands above:
#   0x004A9D6C is the per-player controller vtable; slot +0x10 is the
#   physical-value getter sub_0015F5B0, and that function's address appears
#   exactly once in the whole image -- in that slot.
CTRL_VTABLE = 0x004A9D6C
PHYS_GETTER = 0x0015F5B0
PHYS_SLOT = 0x10
ITEM_NAMES = 0x0053FEBC          # HEALTH_ITEM, ENERGY_ITEM, XTREME_PIP, ...
NOT_A_VA = 0xF0000000            # in no section


def selftest() -> int:
    failures = []

    def check(name, got, want):
        ok = got == want
        print("  %-58s %s" % (name, "ok" if ok else "FAIL (got %r, want %r)"
                              % (got, want)))
        if not ok:
            failures.append(name)

    img = Image(XBE)
    print("image %s: %d sections, %d bytes" % (XBE, len(img.sections), len(img.data)))

    # read: a VA that resolves, and one that must refuse rather than read zeros
    _s, buf = img.read(CTRL_VTABLE + PHYS_SLOT, 4)
    check("read: controller vtable slot +0x10 is the physical getter",
          struct.unpack_from("<I", buf, 0)[0], PHYS_GETTER)
    try:
        img.read(NOT_A_VA, 4)
        check("read: an unmapped VA refuses", "returned data", "refusal")
    except Refused:
        check("read: an unmapped VA refuses", "refusal", "refusal")

    # find: a value that is present exactly once, and one that is absent
    def occurrences(val):
        needle = struct.pack("<I", val)
        n = 0
        for s in img.sections:
            blob = img.data[s.raw:s.raw + s.rsize]
            pos = blob.find(needle)
            while pos != -1:
                n += 1
                pos = blob.find(needle, pos + 1)
        return n

    check("find: the physical getter appears once (in its vtable slot)",
          occurrences(PHYS_GETTER), 1)
    check("find: a value chosen to be absent is reported as absent",
          occurrences(0xF00DFACE), 0)

    # strtab: a real name table resolves, and a code address does not
    check("strtab: the item-type table starts at HEALTH_ITEM",
          img.cstring(struct.unpack_from(
              "<I", img.read(ITEM_NAMES, 4)[1], 0)[0]), "HEALTH_ITEM")
    check("strtab: a code address does not resolve as a string",
          img.cstring(PHYS_GETTER), None)

    # func: a function that exists, and one that does not
    db = functions_db()
    starts = {int(f["start"], 16) for f in db}
    check("func: the physical getter is a detected function",
          PHYS_GETTER in starts, True)
    check("func: an address inside it is not a function start",
          (PHYS_GETTER + 4) in starts, False)

    # vslot: a known physical-getter call and a slot absent from every vtable.
    phys_sites, _nfuncs = vslot_sites(PHYS_SLOT, 8)
    check("vslot: physical getter slot has call sites",
          bool(phys_sites), True)
    absent_sites, _nfuncs = vslot_sites(0x7FC, 8)
    check("vslot: an absent slot has no call sites",
          bool(absent_sites), False)
    present_literal = next(
        int(value, 0) for site in phys_sites for value in site.pushed
        if IMM.match(value))
    check("vslot: literal selection includes a present value",
          bool(select_vslot_sites(phys_sites, [present_literal])), True)
    check("vslot: literal selection excludes an absent value",
          bool(select_vslot_sites(phys_sites, [0x00F00D00])), False)

    # chain: a slot known to be queried on the controller, and one that is not.
    # The negative matters more than the positive here: attribution that credits
    # calls on an unrelated object would make every "found nothing" worthless.
    found = set()
    ngetter = 0
    for slot, a, _fn, _addr in chain_sites(0x4C):
        if slot is None:
            ngetter = a
            continue
        found.add((slot, a))
    check("chain: the accessor slot is reached at all (denominator)",
          ngetter > 0, True)
    check("chain: physical index 0xb is read through controller slot +0x10",
          (0x10, ("0xb",)) in found, True)
    check("chain: a slot no vtable has is credited with nothing",
          any(slot == 0x7FC for slot, _a in found), False)

    # aftercall: the main game singleton has known virtual consumers; a fake
    # direct callee must attribute nothing.
    main_sites = list(returned_sites(0x001E8790))
    check("aftercall: game singleton has attributed virtual calls",
          any(slot is not None for slot, _a, _fn, _addr in main_sites), True)
    absent_returned = list(returned_sites(0x00F00D00))
    check("aftercall: absent direct callee attributes no virtual calls",
          any(slot is not None for slot, _a, _fn, _addr in absent_returned), False)

    print("selftest: %d check(s) failed" % len(failures))
    return 1 if failures else 0


def cmd_selftest(_args) -> int:
    return selftest()


# --------------------------------------------------------------------------


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def hexint(s):
        return int(s, 16)

    p = sub.add_parser("read", help="dwords or bytes at a virtual address")
    p.add_argument("va", type=hexint)
    p.add_argument("--count", type=int, default=8)
    p.add_argument("--bytes", action="store_true")
    p.set_defaults(fn=cmd_read)

    p = sub.add_parser("find", help="every 4-byte occurrence of a value")
    p.add_argument("values", nargs="+", type=hexint)
    p.set_defaults(fn=cmd_find)

    p = sub.add_parser("vtable", help="vtable slots, named through functions.json")
    p.add_argument("va", type=hexint)
    p.add_argument("--count", type=int, default=16)
    p.set_defaults(fn=cmd_vtable)

    p = sub.add_parser("strtab", help="a table of pointers to strings")
    p.add_argument("va", type=hexint)
    p.add_argument("--count", type=int, default=16)
    p.set_defaults(fn=cmd_strtab)

    p = sub.add_parser("func", help="one function's disassembly, or its callers")
    p.add_argument("addr", type=hexint)
    p.add_argument("--callers", action="store_true")
    p.set_defaults(fn=cmd_func)

    p = sub.add_parser("vslot", help="call sites through a vtable slot")
    p.add_argument("slot", type=hexint)
    p.add_argument("--imm", type=lambda s: int(s, 0), action="append",
                   help="show sites pushing this literal (repeatable)")
    p.add_argument("--window", type=int, default=8,
                   help="instructions of look-back for pushes (default 8)")
    p.add_argument("--histogram", action="store_true",
                   help="print every literal seen at this slot")
    p.add_argument("--show-sites", action="store_true",
                   help="print every matching call and its look-back window")
    p.set_defaults(fn=cmd_vslot)

    p = sub.add_parser("chain", help="calls made on the object a slot returned")
    p.add_argument("via", type=hexint, help="accessor slot, e.g. 0x4c")
    p.add_argument("--slot", type=hexint, help="show only this slot on the result")
    p.add_argument("--window", type=int, default=25)
    p.add_argument("--show-sites", action="store_true",
                   help="list each attributed call instead of examples only")
    p.set_defaults(fn=cmd_chain)

    p = sub.add_parser(
        "aftercall", help="virtual calls on an object a direct call returned")
    p.add_argument("callee", type=hexint,
                   help="direct accessor function, e.g. 0x1e8790")
    p.add_argument("--slot", type=hexint, help="show only this result slot")
    p.add_argument("--window", type=int, default=25)
    p.add_argument("--show-sites", action="store_true")
    p.set_defaults(fn=cmd_aftercall)

    p = sub.add_parser("selftest", help="prove the queries can answer both ways")
    p.set_defaults(fn=cmd_selftest)

    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except Missing as exc:
        print("xbe_query: %s" % exc, file=sys.stderr)
        return SKIP
    except Refused as exc:
        print("xbe_query: %s" % exc, file=sys.stderr)
        return REFUSE


if __name__ == "__main__":
    raise SystemExit(main())
