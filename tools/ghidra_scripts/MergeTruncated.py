#@runtime Jython
# Repair a function whose body was cut off by a spurious function starting
# inside it -- the inverse of SplitFunction.py.
#
# The symptom is a function whose last instruction is not a terminator. Its
# emitted C then falls off the end and returns with whatever ESP the partial
# body left, and the failure lands at some later RET popping the wrong word,
# thousands of instructions away. That is exactly how the native run's stall
# was finally traced (issue #13).
#
# The repair: remove the function that starts inside it and re-create the outer
# one, letting Ghidra follow control flow to the real end.
#
# The safety check that matters. The inner "function" is only spurious if
# nothing CALLS it -- if it has real callers it is a genuine entry point and
# the outer function is legitimately non-contiguous, which is a different
# problem and must not be papered over by deleting a function other code needs.
# Those are reported and skipped, not forced.
#
# ENV: MERGE_FUNCS = comma-separated hex addresses of the TRUNCATED functions
import os
import caselabel
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.symbol import RefType

monitor = ConsoleTaskMonitor()
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
rm = prog.getReferenceManager()
af = prog.getAddressFactory().getDefaultAddressSpace()

spec = os.environ.get("MERGE_FUNCS", "").strip()
if not spec:
    print("MERGE: MERGE_FUNCS is empty -- merged NOTHING")
    raise SystemExit

def count_body(fn):
    """Instructions actually in the body -- the only measure of whether a
    merge repaired anything."""
    if fn is None:
        return 0
    n = 0
    for _ in listing.getInstructions(fn.getBody(), True):
        n += 1
    return n


fixed = skipped = failed = absorbed = 0
targets = spec.replace(",", " ").split()

for tok in targets:
    addr = af.getAddress(tok)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        # Usually not a failure: an earlier merge in this same run absorbed it.
        # Calling that "failed" would report a cascade as breakage, and the
        # count is the thing anyone reads.
        owner = fm.getFunctionContaining(addr)
        if owner is not None:
            print("MERGE: %s was absorbed by an earlier merge into %s"
                  % (tok, owner.getName()))
            absorbed += 1
        else:
            print("MERGE: %s is not a function start and is in no function" % tok)
            failed += 1
        continue
    # Is this body actually TRUNCATED? A body ending in a terminator ends for
    # a reason, and absorbing what follows it is not a repair -- it deletes a
    # neighbouring function and re-creates the same body. This check did not
    # exist: the message asserted "without a terminator" and never looked, and
    # on that basis a 3-byte padding function was absorbed into a body ending
    # in RET and reported as "1 repaired".
    last = None
    for i in listing.getInstructions(fn.getBody(), True):
        last = i
    if last is not None and caselabel.is_terminator(last.getMnemonicString()):
        print("MERGE: %s ends at %s with `%s`, which is a TERMINATOR -- the "
              "body is complete, not truncated, so there is nothing here to "
              "absorb. Skipped."
              % (tok, last.getAddress(), last.toString()))
        skipped += 1
        continue
    before_n = count_body(fn)

    # Ask about the address the body FALLS INTO, not about the next function.
    #
    # Those are not the same address, and the difference decided every case
    # this script got wrong. 0x0042b890 ends at 0x0042bab0 and falls into
    # 0x0042bab1, which belongs to no function at all; the next FUNCTION is
    # 0x0042bac0, fifteen bytes further on, and it is called, so the caller
    # check below fired and the whole thing was declared "genuinely
    # non-contiguous" and skipped -- a verdict about a function that has
    # nothing to do with the defect. Three others absorbed a next-function that
    # was nowhere near the fall-through and reported "did not grow the body".
    #
    # The fall-through address is exactly what the emitter computes, so the two
    # halves of this repair now name the same byte.
    end = last.getAddress()
    ft = last.getMaxAddress().add(1)
    owner = fm.getFunctionContaining(ft)
    at_ft = fm.getFunctionAt(ft)

    if owner is not None and at_ft is None:
        # It runs into the MIDDLE of another function. Absorbing that would
        # delete a function other code enters at its own entry point.
        print("MERGE: %s ends at %s and falls into %s, which is INSIDE %s "
              "(entry %s), not at its start -- a shared tail, not a truncated "
              "body. Skipped; this is what --split-at and enter-at-label are "
              "for." % (tok, end, ft, owner.getName(), owner.getEntryPoint()))
        skipped += 1
        continue

    if at_ft is None:
        # The GAP case: the fall-through lands on bytes no function claims.
        # There is nothing to absorb -- the repair is to let Ghidra follow the
        # flow, which means clearing this function's own body plus the
        # unclaimed run and disassembling again. The clear stops before the
        # next function so nothing that belongs to somebody else is touched.
        it = fm.getFunctions(ft, True)
        nf = it.next() if it.hasNext() else None
        if nf is not None:
            limit = nf.getEntryPoint().subtract(1)
        else:
            blk = prog.getMemory().getBlock(ft)
            if blk is None:
                print("MERGE: %s falls into %s, which is not in any memory "
                      "block -- the body ends at the section edge, a different "
                      "problem" % (tok, ft))
                skipped += 1
                continue
            limit = blk.getEnd()
        gap = limit.subtract(ft) + 1
        print("MERGE: %s ends at %s without a terminator and falls into %s, "
              "which NO function claims; re-disassembling over the %d "
              "unclaimed byte(s) up to %s" % (tok, end, ft, gap, limit))
        fm.removeFunction(addr)
        listing.clearCodeUnits(addr, limit, False)
        DisassembleCommand(addr, None, True).applyTo(prog, monitor)
        if CreateFunctionCmd(addr).applyTo(prog, monitor):
            f2 = fm.getFunctionAt(addr)
            ok, msg = caselabel.merge_outcome("the unclaimed run at %s" % ft,
                                              before_n, count_body(f2))
            print("MERGE:   %s" % msg)
            if ok:
                fixed += 1
            else:
                failed += 1
        else:
            print("MERGE:   FAILED to re-create a function at %s -- its body "
                  "is now unattributed, which is worse than before" % tok)
            failed += 1
        continue

    nxt = at_ft
    # Does anything CALL the inner function? A call means it is a real entry
    # point and this is not the repair to apply.
    callers = 0
    for r in rm.getReferencesTo(nxt.getEntryPoint()):
        if r.getReferenceType().isCall():
            callers += 1
    if callers:
        print("MERGE: %s falls into %s, which has %d CALL reference(s) -- "
              "a real function, so %s is genuinely non-contiguous. Skipped."
              % (tok, nxt.getName(), callers, tok))
        skipped += 1
        continue

    inner = nxt.getEntryPoint()
    nxt_name = nxt.getName()
    inner_end = nxt.getBody().getMaxAddress()
    print("MERGE: %s ends at %s without a terminator; absorbing %s (%s..%s)"
          % (tok, end, nxt.getName(), inner, inner_end))
    fm.removeFunction(inner)
    fm.removeFunction(addr)
    listing.clearCodeUnits(addr, inner_end, False)
    DisassembleCommand(addr, None, True).applyTo(prog, monitor)
    if CreateFunctionCmd(addr).applyTo(prog, monitor):
        f2 = fm.getFunctionAt(addr)
        ok, msg = caselabel.merge_outcome(nxt_name, before_n, count_body(f2))
        print("MERGE:   %s" % msg)
        if ok:
            fixed += 1
        else:
            failed += 1
    else:
        print("MERGE:   FAILED to re-create a function at %s -- its body is "
              "now unattributed, which is worse than before" % tok)
        failed += 1

print("MERGE: %d repaired, %d already absorbed by an earlier merge, %d skipped "
      "(complete, or the inner function is real), %d that changed nothing or "
      "failed, of %d"
      % (fixed, absorbed, skipped, failed, len(targets)))
