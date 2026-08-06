#@runtime Jython
# Rebuild a function's BODY from its own control flow, so a switch's case
# blocks belong to it again.
#
# The problem this solves is not a boundary that is too long or too short --
# MergeTruncated.py and SplitFunction.py handle those. It is a body with HOLES.
#
# XMen2.exe FUN_005fac10 is the worked example (issue #21, C123). It spans
# 0x005fac10..0x005fb23c and contains 426 instructions, but NOT 0x005facd5,
# 0x005face5, 0x005facf6 or 0x005fad07 -- its own switch case labels, reached
# through `JMP dword ptr [EAX*0x4 + 0x5fb240]`. Those addresses sit inside its
# range and belong to no function at all, left orphaned when earlier sessions
# seeded them out and a later merge could not take them back (--merge absorbs
# inner FUNCTIONS, and they are no longer functions).
#
# With no instruction there, recomp.py emits no label for it, the function's
# local jump dispatcher has no case for it, and the runtime reports a case
# label as "no recompiled body at <addr>" -- which is what sends the discovery
# loop off to seed it and carve the function apart again.
#
# The repair: delete the function object (NOT the instructions) and re-create
# it at the same entry, which makes Ghidra re-walk the flow from scratch,
# following the jump tables it has since resolved.
#
# WHAT IT REFUSES TO DO. Deleting a function loses whatever was attached to it,
# so this will not touch one that has a name a human gave it -- anything not
# matching Ghidra's own FUN_/SUB_ default. And it re-checks the body
# AFTERWARDS: if the hole it was asked to close is still a hole, it says so and
# counts a failure, because "ran successfully and changed nothing" is the
# outcome that would otherwise read as a fix.
#
# WHY RE-CREATING ALONE IS NOT ENOUGH. On the first run this reported
# "+0, -0" and its own postcondition said the holes were still holes. Ghidra
# had never resolved the computed jump, so there are no references out of the
# `JMP` and the flow walk simply stops there. Re-creating re-walks the same
# flow and finds the same 426 instructions.
#
# So the table has to be handed over first: read the dwords at the JMP's
# displacement, add a COMPUTED_JUMP reference to each, disassemble the targets,
# and only then re-create. The flow walk then follows them.
#
# HOW THE TABLE'S LENGTH IS DECIDED, since guessing it wrong either misses
# cases or drags unrelated code in: entries are taken while they stay inside
# the SAME address range as the jump itself and disassemble as code. The
# tables at 0x005fb240 and 0x005fb250 in XMen2.exe are adjacent, so a fixed
# count would run from one into the next -- this stops at the first entry that
# is not a plausible in-range target, and REPORTS how many it took.
#
# AND HANDING THE TABLE OVER IS STILL NOT ENOUGH when a case label has itself
# been seeded as a FUNCTION. Ghidra will not absorb another function's entry
# point, so the flow walk stops there and the case block -- plus everything
# that falls through from it -- stays outside the container for good.
# 0x0066cf4e wired all 10 of its entries and still ended with a 127-byte hole,
# because entry 0 (0x0066cf79) was FUN_0066cf79: one `CMP dword ptr [EBP+8],3`
# with no terminator (issue #27). So each wired target that is a function is
# put to caselabel.classify(), and un-made when nothing CALLS it and nobody has
# named it. That rule is tested in tests/test_caselabel.py.
#
# ENV: RECREATE_FUNCS = comma-separated hex entry addresses
#      RECREATE_EXPECT = optional comma-separated hex addresses that MUST be
#                        inside a body afterwards; the check that makes a
#                        negative result visible
#      RECREATE_JUMPTABLES = 1 to resolve computed jumps first (see above)
import os
import re
import caselabel
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.program.model.symbol import RefType, SourceType
from ghidra.util.task import ConsoleTaskMonitor

monitor = ConsoleTaskMonitor()
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
af = prog.getAddressFactory().getDefaultAddressSpace()

DEFAULT_NAME = caselabel.DEFAULT_NAME


def addr(x):
    return af.getAddress(x)


def body_addrs(fn):
    """The set of instruction addresses actually in the function's body."""
    out = set()
    if fn is None:
        return out
    for r in fn.getBody().getAddressRanges():
        a = r.getMinAddress()
        while a is not None and a.compareTo(r.getMaxAddress()) <= 0:
            ins = listing.getInstructionAt(a)
            if ins is not None:
                out.add(ins.getAddress().getOffset())
                a = ins.getMaxAddress().add(1)
            else:
                a = a.add(1)
    return out


DISP = re.compile(r"\[\s*[A-Z]{3}\s*\*\s*0x4\s*\+\s*(0x[0-9a-fA-F]+)\s*\]")
mem = prog.getMemory()
rm = prog.getReferenceManager()


def resolve_jump_tables(fn):
    """Give Ghidra the targets of this function's computed jumps.

    Returns (tables, entries) actually wired up. Reports what it did, because
    "0 tables" and "never looked" have to be distinguishable.
    """
    tables = entries = 0
    body = fn.getBody()
    it = listing.getInstructions(body, True)
    jumps = []
    while it.hasNext():
        ins = it.next()
        if ins.getMnemonicString().upper() != "JMP":
            continue
        m = DISP.search(ins.toString())
        if m:
            jumps.append((ins, int(m.group(1), 16)))

    if not jumps:
        print("RECREATE:   no computed jumps of the form JMP [reg*4 + <imm>] "
              "in this function -- nothing to resolve")
        return 0, 0

    for ins, tbl in jumps:
        n = 0
        cases = []
        while True:
            slot = addr(tbl + n * 4)
            if not mem.contains(slot):
                break
            try:
                tgt_off = mem.getInt(slot) & 0xFFFFFFFF
            except Exception:
                break
            tgt = addr(tgt_off)
            # Stop at the first entry that is not a plausible in-range target.
            # This is what keeps two adjacent tables apart. The rule itself is
            # in caselabel.py, where it is tested.
            if not mem.contains(tgt):
                break
            blk = mem.getBlock(tgt)
            if not caselabel.plausible_entry(
                    tgt_off, blk == mem.getBlock(ins.getAddress()),
                    blk.isExecute()):
                break
            if listing.getInstructionAt(tgt) is None:
                DisassembleCommand(tgt, None, True).applyTo(prog, monitor)
            rm.addMemoryReference(ins.getAddress(), tgt,
                                  RefType.COMPUTED_JUMP,
                                  SourceType.ANALYSIS, 0)
            cases.append(tgt)
            n += 1
            if n > 512:            # a runaway table is a bug, not a switch
                print("RECREATE:   table at 0x%08x exceeded 512 entries -- "
                      "stopping, this is not a switch" % tbl)
                break
        print("RECREATE:   jump at 0x%08x -> table 0x%08x: %d entr%s wired"
              % (ins.getAddress().getOffset(), tbl, n,
                 "y" if n == 1 else "ies"))
        unmade, kept = unmake_case_functions(fn, tbl, cases)
        if n:
            tables += 1
            entries += n
    return tables, entries


def unmake_case_functions(fn, tbl, cases):
    """Delete function objects sitting on this switch's own case labels.

    Wiring the table is not enough on its own: Ghidra will not absorb another
    function's entry point, so a case label that some earlier pass seeded as a
    function keeps its block -- and everything falling through from it --
    outside the container permanently. 0x0066cf4e wired 10 entries and still
    ended with a 127-byte hole because entry 0 was FUN_0066cf79 (issue #27).

    The decision for each target is caselabel.classify(), which is tested in
    tests/test_caselabel.py. This function only gathers the facts it weighs
    and carries out the verdict.
    """
    unmade = kept = 0
    for tgt in cases:
        inner = fm.getFunctionAt(tgt)
        callers = 0
        for r in rm.getReferencesTo(tgt):
            if r.getReferenceType().isCall():
                callers += 1
        action, msg = caselabel.classify(
            container=fn.getEntryPoint().getOffset(),
            container_name=fn.getName(),
            table=tbl,
            target=tgt.getOffset(),
            inner_name=inner.getName() if inner is not None else None,
            call_refs=callers)
        if action == "absorb":
            print("RECREATE:   %s" % msg)
            fm.removeFunction(tgt)
            unmade += 1
        elif action in ("keep-called", "keep-named"):
            print("RECREATE:   %s" % msg)
            kept += 1
    print("RECREATE:   %s" % caselabel.summarize(tbl, len(cases), unmade, kept))
    return unmade, kept


spec = os.environ.get("RECREATE_FUNCS", "").strip()
if not spec:
    # Refuse rather than report success over an empty list.
    print("RECREATE: RECREATE_FUNCS is empty -- recreated NOTHING")
    raise SystemExit(1)

expect = [int(x, 16) for x in
          os.environ.get("RECREATE_EXPECT", "").replace(",", " ").split()]

fixed = skipped = failed = 0
recreated = []
for tok in spec.replace(",", " ").split():
    # The caller writes these as 0x… or bare; normalise so the log never says
    # "0x0x0066cf4e".
    tok = tok[2:] if tok.lower().startswith("0x") else tok
    ep = addr(int(tok, 16))
    fn = fm.getFunctionAt(ep)
    if fn is None:
        print("RECREATE: 0x%s is not a function entry -- skipped" % tok)
        skipped += 1
        continue
    name = fn.getName()
    if not DEFAULT_NAME.match(name):
        print("RECREATE: %s at 0x%s has a NON-DEFAULT name; refusing to "
              "delete it. Rename it back or do this by hand." % (name, tok))
        skipped += 1
        continue

    before = body_addrs(fn)
    if os.environ.get("RECREATE_JUMPTABLES") == "1":
        resolve_jump_tables(fn)
    fm.removeFunction(ep)
    cmd = CreateFunctionCmd(None, ep, None, ghidra.program.model.symbol
                            .SourceType.ANALYSIS)
    cmd.applyTo(prog, monitor)
    fn = fm.getFunctionAt(ep)
    after = body_addrs(fn)

    if fn is None:
        print("RECREATE: 0x%s could NOT be re-created -- the function is now "
              "GONE, which is worse than before. Investigate before rebuilding."
              % tok)
        failed += 1
        continue
    gained = len(after - before)
    lost = len(before - after)
    print("RECREATE: 0x%s %s -> %d instructions (+%d, -%d)"
          % (tok, name, len(after), gained, lost))
    recreated.append(ep)
    fixed += 1

print("RECREATE: %d recreated, %d skipped, %d failed, of %d requested"
      % (fixed, skipped, failed, len(spec.replace(",", " ").split())))

# The postcondition. Without this the script can run cleanly, change nothing,
# and read as a repair.
#
# It asks whether the address is inside one of the functions THIS RUN
# re-created, not merely inside some function. "Inside some function" is the
# check that cannot fail for the defect this script now repairs: a case label
# seeded as a function is already inside a function -- itself -- so the old
# wording would have certified 0x0066cf79 as fixed while the hole was still
# there (issue #27).
if expect:
    missing = []
    for a in expect:
        owner = fm.getFunctionContaining(addr(a))
        if owner is None or owner.getEntryPoint() not in recreated:
            missing.append((a, owner.getName() if owner else None))
    if missing:
        print("RECREATE: POSTCONDITION FAILED -- %d of %d expected address(es) "
              "are NOT inside any function re-created here: %s"
              % (len(missing), len(expect),
                 ", ".join("0x%08x (in %s)" % (a, n or "NO function")
                           for a, n in missing)))
    else:
        print("RECREATE: postcondition OK -- all %d expected address(es) are "
              "now inside a function re-created here" % len(expect))
