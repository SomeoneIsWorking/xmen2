#!/usr/bin/env python3
"""Check src/d3d8/d3d8_abi.h against the two things that can contradict it.

The host D3D8 layer stands entirely on one assumption: that the PC
IDirect3DDevice8 vtable has the method order and argument counts written down
in `src/d3d8/d3d8_abi.h`.  Get an entry wrong and the guest calls slot N, lands
on a different method and pops the wrong number of dwords -- which shifts the
guest stack and faults somewhere with no connection to DirectX.  I038 records
the shortcut that does NOT work (deriving counts by counting pushes with the
interface unknown), so this checks the table two other ways instead:

  THE GAME     libIGGfx's own call sites.  Only sites where the object is
               provably the device -- the register was loaded from
               [this+0x144] and the vtable from that register -- are used, which
               is the exact thing I038 lacked.  Push counts at those sites are
               compared with the table.

  A REAL d3d8.h  when the machine has one.  Nothing is committed here: the
               checker says loudly when it found no header, because a run that
               verified NOTHING from a header must not read like one that
               passed.

Both checks print their DENOMINATOR and their blind spots.  "No disagreements"
out of two attributable call sites means something very different from the same
words out of two hundred, and the difference is the only thing that makes the
answer worth anything.

    tools/d3d8_abi_check.py                 # both checks
    tools/d3d8_abi_check.py --selftest      # prove the checks can FAIL

Exit codes: 0 all checks that could run agreed; 1 a disagreement; 2 nothing
could be checked at all.
"""

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ABI_H = os.path.join(ROOT, "src", "d3d8", "d3d8_abi.h")
GFX_JSON = os.path.join(ROOT, "scratch", "analysis", "libIGGfx.json")

# Where a real PC d3d8.h may live.  $D3D8_HEADER overrides.  None of these is
# committed or required; the point of the list is that "no header" is reported
# with the places that were looked in.
HEADER_CANDIDATES = [
    "/usr/i686-w64-mingw32/sys-root/mingw/include/d3d8.h",
    "/usr/x86_64-w64-mingw32/sys-root/mingw/include/d3d8.h",
    "/usr/include/wine/windows/d3d8.h",
    "/usr/lib/zig/libc/include/any-windows-any/d3d8.h",
]

# The engine keeps its IDirect3DDevice8 here; see docs/info/claims C127/C128 and
# igDxVisualContext::userInstantiate, which stores IDirect3D8 at +0x140.
DEVICE_FIELD = 0x144
D3D8_FIELD = 0x140


# ---------------------------------------------------------------- the table


def parse_abi(path):
    """{interface: [(slot, name, args)]} from the X-macro tables."""
    text = open(path).read()

    # Resolve the two prefix macros by textual substitution, the same way the
    # preprocessor will: a checker reading a different table from the compiler
    # would be worse than no checker.
    def body(name):
        m = re.search(r"#define\s+%s\(X\)(.*?)(?=\n#define|\n#endif)" % name, text, re.S)
        return m.group(1) if m else ""

    resolved = {}
    ifaces = re.findall(r"#define\s+D3D8_IFACE_(\w+)\(X\)(.*?)(?=\n#define|\n#endif)", text, re.S)
    for name, blob in ifaces:
        for macro in ("D3D8_BASETEXTURE_PREFIX", "D3D8_RESOURCE_PREFIX"):
            while macro + "(X)" in blob:
                blob = blob.replace(macro + "(X)", body(macro))
        entries = []
        for slot, meth, args in re.findall(r"X\(\s*(\d+)\s*,\s*(\w+)\s*,\s*(\d+)\s*\)", blob):
            entries.append((int(slot), meth, int(args)))
        resolved[name] = entries
    return resolved


# ------------------------------------------------------- a real d3d8.h


def find_header():
    env = os.environ.get("D3D8_HEADER")
    if env:
        return env if os.path.exists(env) else None
    for p in HEADER_CANDIDATES:
        if os.path.exists(p):
            return p
    return None


def parse_header(path):
    src = open(path, errors="replace").read()
    out = {}
    for name, _parent, blob in re.findall(
        r"DECLARE_INTERFACE_IID_\((\w+),(\w+),.*?\n(.*?)\n\};", src, re.S
    ):
        blob = blob.replace("\n", " ")
        methods = []
        for part in re.split(r"(?=STDMETHOD)", blob):
            part = part.strip()
            if not part.startswith("STDMETHOD"):
                continue
            m = re.match(r"STDMETHOD(?:_\(\s*[^,]+?\s*,\s*(\w+)\s*\)|\(\s*(\w+)\s*\))", part)
            if not m:
                continue
            meth = m.group(1) or m.group(2)
            a = re.search(r"\(\s*THIS(_)?\s*(.*?)\)\s*PURE\s*;", part, re.S)
            if not a:
                continue
            if not a.group(1) or not a.group(2).strip():
                nargs = 0
            else:
                depth, nargs = 0, 1
                for ch in a.group(2):
                    if ch in "(<":
                        depth += 1
                    elif ch in ")>":
                        depth -= 1
                    elif ch == "," and depth == 0:
                        nargs += 1
            methods.append((len(methods), meth, nargs))
        out[name] = methods
    return out


def check_header(abi, perturb=None):
    path = find_header()
    if not path:
        print("HEADER CROSS-CHECK: SKIPPED -- no d3d8.h found.")
        print("  Looked in $D3D8_HEADER and:")
        for p in HEADER_CANDIDATES:
            print("    %s" % p)
        print(
            "  NOTHING was verified against a header. The game cross-check "
            "below is independent of this\n  and still stands on its own."
        )
        return None

    hdr = parse_header(path)
    print("HEADER CROSS-CHECK against %s" % path)
    checked = disagreed = 0
    missing = []
    for iface, entries in sorted(abi.items()):
        if iface not in hdr:
            missing.append(iface)
            continue
        theirs = hdr[iface]
        if len(theirs) != len(entries):
            print(
                "  %-26s DISAGREES on method COUNT: table %d, header %d"
                % (iface, len(entries), len(theirs))
            )
            disagreed += 1
            continue
        for (s, n, a), (_hs, hn, ha) in zip(entries, theirs, strict=True):
            checked += 1
            mine = (n, a)
            if perturb and perturb[0] == iface and perturb[1] == s:
                mine = (n, a + 1)
            if mine[0] != hn or mine[1] != ha:
                print(
                    "  %-26s slot %-3d table %s(%d args) vs header %s(%d args)"
                    % (iface, s, mine[0], mine[1], hn, ha)
                )
                disagreed += 1
    print(
        "  %d method(s) compared across %d interface(s); %d disagreement(s)."
        % (checked, len(abi) - len(missing), disagreed)
    )
    if missing:
        print("  NOT compared (the header does not declare them): %s" % ", ".join(missing))
    print(
        "  Blind spot: this compares NAMES and ARGUMENT COUNTS. A method "
        "whose arguments are the\n  same count but different types would "
        "pass here and still be wrong at the call site."
    )
    return disagreed


# ------------------------------------------------------------- the game

REG = r"E[A-D][XI]|E[SD]I|EBP|ESP"
RE_FIELD_LOAD = re.compile(r"^MOV (%s),dword ptr \[(%s) \+ 0x([0-9a-f]+)\]$" % (REG, REG))
RE_DEREF = re.compile(r"^MOV (%s),dword ptr \[(%s)\]$" % (REG, REG))
RE_VCALL = re.compile(r"^CALL dword ptr \[(%s) \+ 0x([0-9a-f]+)\]$" % REG)
RE_VCALL0 = re.compile(r"^CALL dword ptr \[(%s)\]$" % REG)
RE_WRITES = re.compile(r"^(?:MOV|LEA|XOR|ADD|SUB|AND|OR|POP|MOVZX|MOVSX|IMUL) (%s)," % REG)


def scan_game(abi, perturb=None):
    if not os.path.exists(GFX_JSON):
        print(
            "GAME CROSS-CHECK: REFUSED -- %s does not exist, so NO call "
            "site was examined." % GFX_JSON
        )
        print("  Run tools/ghidra_export.sh libIGGfx first. This is not a pass.")
        return None

    doc = json.load(open(GFX_JSON))
    device_methods = {s: (n, a) for s, n, a in abi["IDirect3DDevice8"]}

    checked = disagreed = 0
    unattributable = 0
    device_sites = 0
    per_method = {}
    problems = []
    inconclusive = []

    for fn in doc["functions"]:
        ins = fn["ins"]
        # Registers currently holding the device pointer, and registers holding
        # a vtable loaded FROM one of those.  Reset conservatively: any write to
        # a register clears its provenance.
        is_device, is_vtable = set(), set()
        origin = {}  # register -> index where its provenance was set
        for idx, i in enumerate(ins):
            t = i["t"]

            m = RE_FIELD_LOAD.match(t)
            if m:
                dst, _src, off = m.group(1), m.group(2), int(m.group(3), 16)
                is_device.discard(dst)
                is_vtable.discard(dst)
                if off == DEVICE_FIELD:
                    is_device.add(dst)
                    origin[dst] = idx
                continue

            m = RE_DEREF.match(t)
            if m:
                dst, src = m.group(1), m.group(2)
                is_device.discard(dst)
                is_vtable.discard(dst)
                if src in is_device:
                    is_vtable.add(dst)
                    origin[dst] = origin.get(src, idx)
                continue

            m = RE_VCALL.match(t) or RE_VCALL0.match(t)
            if m:
                reg = m.group(1)
                off = int(m.group(2), 16) if m.lastindex and m.re is RE_VCALL else 0
                if reg not in is_vtable:
                    # Not attributable to the device: it may be any COM object.
                    # Counted, never guessed -- this is precisely the class I038
                    # got wrong by treating every offset as the device's.
                    if RE_VCALL.match(t):
                        unattributable += 1
                    continue
                device_sites += 1
                slot = off // 4
                pushes, clean = count_pushes(ins, idx, origin.get(reg))
                if not clean or pushes == 0:
                    unattributable += 1
                    continue
                declared = device_methods.get(slot)
                if declared is None:
                    problems.append(
                        "0x%08x calls device slot %d (offset 0x%x), "
                        "which is past the 97 the table declares" % (i["a"], slot, off)
                    )
                    disagreed += 1
                    continue
                name, args = declared
                if perturb and perturb[1] == slot:
                    args += 1
                checked += 1
                rec = per_method.setdefault(name, [0, 0, 0])
                seen = pushes - 1
                if seen == args:
                    rec[0] += 1
                elif seen > args:
                    # INCONCLUSIVE, not a disagreement. A backward push scan
                    # cannot tell an argument from a callee-saved register the
                    # branch pushed (libIGGfx 0x10045a5d PUSHes EDI on one arm
                    # of a function and POPs it at 0x10045ad4) nor from an
                    # enclosing call's staging. Every over-count examined by
                    # hand has been one of those two.
                    rec[1] += 1
                    inconclusive.append(
                        "0x%08x %s: %d push(es) seen, %d argument(s) declared "
                        "-- over-count, so a register save or outer staging"
                        % (i["a"], name, pushes, args)
                    )
                else:
                    # UNDER-count is the sound signal: a call site physically
                    # cannot push fewer dwords than the callee pops, so this
                    # can only mean the table declares more arguments than the
                    # method has.
                    rec[2] += 1
                    problems.append(
                        "0x%08x %s: %d push(es) = %d argument(s) + this, but "
                        "the table declares %d. A call site cannot push fewer "
                        "than the callee pops." % (i["a"], name, pushes, seen, args)
                    )
                    disagreed += 1
                continue

            m = RE_WRITES.match(t)
            if m:
                is_device.discard(m.group(1))
                is_vtable.discard(m.group(1))
            elif t.startswith("CALL"):
                # A call clobbers the volatile registers, so no provenance
                # survives it.
                is_device.clear()
                is_vtable.clear()

    print("GAME CROSS-CHECK against libIGGfx's own call sites")
    print(
        "  %d call site(s) provably on the device (register loaded from "
        "[this+0x%x], vtable from it)." % (device_sites, DEVICE_FIELD)
    )
    print("  %d of those had a readable push sequence and were compared." % checked)
    print(
        "  %d indirect vtable call(s) were NOT attributable to the device "
        "and were left alone." % unattributable
    )
    if checked:
        exact = sorted(n for n, v in per_method.items() if v[0])
        print(
            "  %d method(s) CONFIRMED exactly by at least one call site: %s"
            % (len(exact), ", ".join(exact))
        )
    print(
        "  %d site(s) over-counted (inconclusive, see below); %d site(s) "
        "UNDER-counted (a real disagreement)." % (len(inconclusive), disagreed)
    )
    for p in problems[:20]:
        print("    !! %s" % p)
    if len(problems) > 20:
        print("    !! ... and %d more" % (len(problems) - 20))
    for p in inconclusive[:6]:
        print("    ?  %s" % p)
    if len(inconclusive) > 6:
        print("    ?  ... and %d more" % (len(inconclusive) - 6))
    print(
        "  Blind spots, stated: arguments staged with MOV into stack slots "
        "rather than PUSH are counted\n  as unattributable, not as zero; a "
        "method the engine never calls is not checked at all; a method\n  "
        "called with the right COUNT of wrong arguments passes here; and an "
        "over-count proves nothing\n  in either direction, which is why "
        "only under-counts fail this check."
    )
    return disagreed


def count_pushes(ins, call_idx, floor):
    """PUSHes immediately before ins[call_idx].  Returns (n, clean).

    Walks back over the instructions that stage a call, stopping at anything
    that is neither a PUSH nor a plain register/flag computation.  `clean` is
    False when the scan hit something that could have moved an argument by
    another route -- an ADD ESP, another CALL, a stack-slot MOV -- in which
    case the count is not evidence either way.

    `floor` is the index at which the device pointer was materialised, and the
    scan may not go past it.  Without that floor the walk ran straight through
    the argument staging into the FUNCTION PROLOGUE and counted `PUSH EBX/EBP/
    ESI/EDI` as arguments: libIGGfx 0x1003f135 is `PUSH ECX; PUSH 0xf; PUSH
    EAX; CALL [EDX+0xc8]`, an ordinary two-argument SetRenderState, and the
    unfloored scan reported seven arguments for it.  The floor is sound in the
    direction that matters -- a compiler must load `this` before it can push
    it -- but it can UNDERCOUNT if an argument was pushed before the load, so
    a PUSH immediately below the floor makes the site unattributable rather
    than short."""
    n = 0
    k = call_idx - 1
    while k >= 0 and (floor is None or k >= floor):
        t = ins[k]["t"]
        if t.startswith("PUSH"):
            n += 1
            k -= 1
            continue
        if t.startswith("CALL") or t.startswith("RET") or t.startswith("J"):
            break
        if "ESP" in t or "[ESP" in t:
            return n, False
        if t.startswith(
            (
                "MOV",
                "LEA",
                "XOR",
                "TEST",
                "CMP",
                "ADD",
                "SUB",
                "AND",
                "OR",
                "INC",
                "DEC",
                "SHL",
                "SHR",
                "NOP",
                "MOVZX",
                "MOVSX",
                "FLD",
                "FSTP",
                "IMUL",
                "NEG",
                "SETZ",
                "SETNZ",
                "CDQ",
            )
        ):
            k -= 1
            continue
        break
    else:
        # The scan ran out at the floor rather than at a terminator.  If more
        # PUSHes lie below it they may belong to this call (an argument
        # computed before `this` was loaded) or to an enclosing one, and there
        # is no way to tell -- so the site gives no evidence either way.
        j = k
        while j >= 0:
            t = ins[j]["t"]
            if t.startswith("PUSH") or "ESP" in t:
                # Either more pushes that may be this call's, or an argument
                # staged into a stack slot by MOV -- the blind spot I038
                # names. Both mean this site cannot count anything.
                return n, False
            if t.startswith(("CALL", "RET", "J")):
                break
            j -= 1
    return n, True


# ------------------------------------------------------------- self-test


def selftest(abi):
    """Prove both checks can produce the OTHER answer.

    A checker that has only ever printed "no disagreements" has not been shown
    to be able to print anything else.  This shifts one entry of the table by a
    single argument and requires each check to notice."""
    print("=== SELF-TEST: the checks must FAIL on a table that is wrong ===\n")
    perturb = ("IDirect3DDevice8", 50)  # SetRenderState, the busiest method
    fails = 0

    h = check_header(abi, perturb=perturb)
    if h is None:
        print("  (header check could not run, so it proved nothing here)\n")
    elif h == 0:
        print("  FAIL: the header check passed a table with slot 50 wrong.\n")
        fails += 1
    else:
        print("  the header check caught it.\n")

    g = scan_game(abi, perturb=perturb)
    if g is None:
        print("  (game check could not run, so it proved nothing here)")
    elif g == 0:
        print("  FAIL: the game check passed a table with slot 50 wrong.")
        fails += 1
    else:
        print("  the game check caught it.")

    print("\nSELF-TEST %s" % ("FAILED" if fails else "passed"))
    return fails


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--selftest", action="store_true", help="prove the checks can fail, by perturbing the table"
    )
    args = ap.parse_args()

    abi = parse_abi(ABI_H)
    if not abi:
        print("REFUSED: no interface tables were parsed out of %s. Nothing was checked." % ABI_H)
        return 2
    print(
        "Parsed %d interface(s) from %s: %s\n"
        % (
            len(abi),
            os.path.relpath(ABI_H, ROOT),
            ", ".join("%s(%d)" % (k, len(v)) for k, v in sorted(abi.items())),
        )
    )

    if args.selftest:
        return 1 if selftest(abi) else 0

    h = check_header(abi)
    print()
    g = scan_game(abi)
    if h is None and g is None:
        print("\nNOTHING could be checked: no header and no libIGGfx export. This is not a pass.")
        return 2
    total = (h or 0) + (g or 0)
    print(
        "\n%s"
        % (
            "AGREED -- no disagreement in any check that could run."
            if total == 0
            else "DISAGREEMENT: %d. The table and the evidence do not "
            "match; do not dispatch through it." % total
        )
    )
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
