#!/usr/bin/env python3
"""Which of a renderer class's vtable slots reach the DirectX device fields.

    tools/device_slots.py <module>.json <module>.vtab.json --class NAME
        [--fields 0x140,0x144,0x148,0x14c] [--depth 6]
        [--header src/vulkan/igvk_device_slots.h] [--list]

## The question this answers

igVkVisualContext inherits igDx8VisualContext's vtable wholesale (C119) and
then punches out the slots that would call through a Direct3D device this host
never created.  Deciding WHICH slots those are by reading 334 functions is not
practical, and guessing is worse than useless: a slot wrongly inherited
dereferences NULL at some field offset, far from the vtable that caused it.

So the set is computed: a slot is device-touching if its function, or anything
it reaches by direct call within `--depth` hops, mentions one of the device
field offsets off a register.  `this+0x140` is igDxVisualContext's IDirect3D8
and 0x144/0x148/0x14c its neighbours.

## What this is NOT

**A LOWER BOUND, and the reasons are worth stating because each one is a way
the answer can be too small:**

  * Only DIRECT calls are followed.  A slot that reaches the device through a
    function pointer, a vtable dispatch, or an import thunk looks clean here.
  * The match is on the offset CONSTANT, so a device access computed as
    `base+0x40` after the base was already advanced by 0x100 is invisible.
  * Functions the exporter could not translate have no instructions to scan
    and are reported by `--list` as UNSCANNED rather than as clean.

The count is printed with all three denominators for that reason.  "98 slots"
without "of 334 scanned, 0 unscanned, direct calls only" is a number that
cannot be checked.

This regenerates src/vulkan/igvk_device_slots.h, which before this tool
existed was a committed artifact whose generator was not.
"""
import argparse
import json
import re
import sys

# `MOV EAX,dword ptr [ECX + 0x140]`, `LEA EDX,[ESI + 0x148]`, ... -- any
# mention of the offset as a displacement off a register.  Written as a regex
# over Ghidra's instruction text rather than over decoded operands because the
# text is what the exporter records.
DISP = re.compile(r"\[\s*E[A-Z]{2}\s*\+\s*(0x[0-9a-fA-F]+)\s*\]")
CALL = re.compile(r"^CALL\s+(0x[0-9a-fA-F]+)$")
RET = re.compile(r"^RET(?:\s+(0x[0-9a-fA-F]+))?$")
DEV_READ = re.compile(r"^MOV\s+(E[A-Z]{2}),dword ptr \[E[A-Z]{2} \+ (0x14[048c])\]$")
TESTJ = re.compile(r"^TEST\s+(E[A-Z]{2}),\1$")


def guards_device(fn):
    """Does every read of a device field get NULL-checked right after it?

    The pattern the engine uses is literally

        MOV EAX,dword ptr [ESI + 0x144]
        TEST EAX,EAX
        JZ  <skip>

    so this looks for a TEST of the SAME register within three instructions,
    followed by a conditional jump. That is what makes a slot safe to
    super-call with the device left NULL.

    DELIBERATELY CONSERVATIVE, and the direction matters: a slot reported
    unguarded may still be safe, but a slot reported guarded has the check
    in the instruction stream. Getting it wrong the safe way costs a
    transcription; the other way costs a SIGSEGV. Slots with no device read
    at all are reported "-" rather than guarded, because the question does
    not apply to them.
    """
    ins = fn.get("ins", ())
    reads = 0
    for k, i in enumerate(ins):
        m = DEV_READ.match(i.get("t", "").strip())
        if not m:
            continue
        reads += 1
        reg = m.group(1)
        ok = False
        for j in range(k + 1, min(k + 4, len(ins))):
            t = ins[j].get("t", "").strip()
            if TESTJ.match(t) and t.split()[1].split(",")[0] == reg:
                nxt = ins[j + 1].get("t", "").strip() if j + 1 < len(ins) else ""
                if nxt.startswith("J"):
                    ok = True
            if ok:
                break
        if not ok:
            return "UNGUARDED"
    return "guarded" if reads else "-"


def ret_bytes(fn):
    """How many argument bytes the function pops, from its own RET.

    A native override reached through the vtable must pop exactly what the
    function it replaces popped, or the guest stack drifts and the failure
    lands somewhere else entirely (see ark_ret's comment).  So this is read
    out of the binary rather than inferred from a guessed signature.

    Returns (bytes, "ok") or (None, why) -- disagreeing RETs in one function,
    or none at all, are reported rather than resolved, because either means
    the caller must go and look.
    """
    seen = set()
    for ins in fn.get("ins", ()):
        m = RET.match(ins.get("t", "").strip())
        if m:
            seen.add(int(m.group(1), 16) if m.group(1) else 0)
    if not seen:
        return None, "no RET (tail-jump or untranslated)"
    if len(seen) > 1:
        return None, "RETs disagree: %s" % sorted(seen)
    return seen.pop(), "ok"


def load_functions(path):
    with open(path) as f:
        d = json.load(f)
    return {fn["ep"]: fn for fn in d["functions"]}


def touches(fn, fields):
    for ins in fn.get("ins", ()):
        for m in DISP.finditer(ins.get("t", "")):
            if int(m.group(1), 16) in fields:
                return True
    return False


def direct_callees(fn):
    out = []
    for ins in fn.get("ins", ()):
        m = CALL.match(ins.get("t", "").strip())
        if m:
            out.append(int(m.group(1), 16))
    return out


def reaches(ep, fns, fields, depth, memo):
    """True if `ep` or anything it directly calls within `depth` hops touches a
    device field.  Returns (verdict, scanned_any) -- scanned_any is False when
    nothing along the way had instructions, which is the UNSCANNED case."""
    if ep in memo:
        return memo[ep]
    memo[ep] = (False, False)          # cycles resolve to "no", provisionally
    fn = fns.get(ep)
    if fn is None or not fn.get("ins"):
        memo[ep] = (False, False)
        return memo[ep]
    if touches(fn, fields):
        memo[ep] = (True, True)
        return memo[ep]
    if depth <= 0:
        memo[ep] = (False, True)
        return memo[ep]
    for callee in direct_callees(fn):
        hit, _ = reaches(callee, fns, fields, depth - 1, {})
        if hit:
            memo[ep] = (True, True)
            return memo[ep]
    memo[ep] = (False, True)
    return memo[ep]


HEADER = """\
/* GENERATED by tools/device_slots.py -- do not edit.
 *
 * Regenerate with:
 *   python3 tools/device_slots.py scratch/recomp/libIGGfx.json \\
 *       scratch/recomp/libIGGfx.vtab.json --class %(cls)s \\
 *       --header src/vulkan/igvk_device_slots.h
 *
 * The slots of %(cls)s's vtable that reach the DirectX device fields
 * (this+%(fieldlist)s), following DIRECT calls to depth %(depth)d.
 * Everything NOT listed here is platform-neutral engine bookkeeping and is
 * inherited verbatim (C119).
 *
 * %(count)d of %(total)d slots, %(unscanned)d of which could not be scanned at
 * all (no translated instructions) and are therefore NOT in the list.
 *
 * LOWER BOUND, three ways: direct calls only, so a slot reaching the device
 * through a function pointer or a vtable dispatch is absent; the match is on
 * the offset constant, so an access off an already-advanced base is invisible;
 * and the unscanned functions above were never looked at. A slot missing from
 * this list is inherited, and an inherited device-touching slot calls through
 * a device this host never made.
 */
/*
 * How many STACK ARGUMENTS each slot's function pops, from its own RET N.
 * -1 where the body has no RET of its own (a tail jump) or its RETs disagree
 * -- those cannot be answered from the binary and must not be guessed, so a
 * consumer has to refuse rather than pick a number.
 *
 * This exists so a slot can be answered without being implemented: popping
 * the right count is the difference between "did nothing" and "corrupted the
 * guest stack".
 */
static const signed char IGVK_SLOT_ARGS[%(total)d] = {
%(argbody)s};

#define IGVK_DEVICE_SLOT_COUNT %(count)d
static const short IGVK_DEVICE_SLOTS[IGVK_DEVICE_SLOT_COUNT] = {
%(body)s};
"""


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module_json")
    ap.add_argument("vtab_json")
    ap.add_argument("--class", dest="cls", required=True)
    ap.add_argument("--fields", default="0x140,0x144,0x148,0x14c")
    ap.add_argument("--depth", type=int, default=6)
    ap.add_argument("--header", help="write the C header here")
    ap.add_argument("--list", action="store_true",
                    help="print every slot with its name and verdict")
    a = ap.parse_args(argv)

    fields = {int(x, 16) for x in a.fields.split(",")}
    fns = load_functions(a.module_json)
    with open(a.vtab_json) as f:
        vt = json.load(f)
    if a.cls not in vt:
        sys.exit("device_slots: %s has no vtable in %s. Known: %s"
                 % (a.cls, a.vtab_json, ", ".join(sorted(vt))))
    slots = vt[a.cls]["fns"]

    hits, unscanned = [], []
    for i, ep in enumerate(slots):
        hit, scanned = reaches(ep, fns, fields, a.depth, {})
        if hit:
            hits.append(i)
        elif not scanned:
            unscanned.append(i)
        if a.list:
            fn = fns.get(ep)
            verdict = "DEVICE" if hit else ("UNSCANNED" if not scanned else ".")
            if fn is None:
                sig = "?"
            else:
                nb, why = ret_bytes(fn)
                sig = "ret %d" % nb if nb is not None else "RET? %s" % why
            print("  %3d  0x%08x  %-9s  %-10s  %-9s  %s"
                  % (i, ep, verdict, sig,
                     guards_device(fn) if fn else "?",
                     fn["qname"] if fn else "(no function)"))

    print("device_slots: %s -- %d of %d slots reach %s; %d unscanned"
          % (a.cls, len(hits), len(slots), a.fields, len(unscanned)),
          file=sys.stderr)
    if unscanned:
        print("  UNSCANNED slots (absent from the list, so INHERITED): %s"
              % ", ".join(str(i) for i in unscanned), file=sys.stderr)

    if a.header:
        body = ""
        for n in range(0, len(hits), 12):
            body += "    " + "".join("%4d," % s for s in hits[n:n + 12]) + "\n"
        nargs = []
        for ep in slots:
            fn = fns.get(ep)
            nb, _ = ret_bytes(fn) if fn else (None, "")
            nargs.append(-1 if nb is None else nb // 4)
        argbody = ""
        for n in range(0, len(nargs), 16):
            argbody += "    " + "".join("%3d," % a for a in nargs[n:n + 16]) + "\n"
        with open(a.header, "w") as f:
            f.write(HEADER % dict(cls=a.cls, depth=a.depth, count=len(hits),
                                  argbody=argbody,
                                  total=len(slots), unscanned=len(unscanned),
                                  fieldlist="/".join("0x%x" % x
                                                     for x in sorted(fields)),
                                  body=body))
        print("device_slots: wrote %s" % a.header, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
