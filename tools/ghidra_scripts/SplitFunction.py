#@runtime Jython
# Force a function boundary at an address that Ghidra has swallowed into an
# earlier function.
#
# AddFunctions.py refuses these: it reports "already inside a function" and
# creates nothing, so a discovery loop that keeps seeding the address spins
# forever. The cause is a desynchronised disassembly -- the earlier function's
# instruction stream ran past its real end and decoded the target's bytes as
# part of some other instruction, so the target is not even an instruction
# boundary in the database.
#
# This clears the code units from the target to the end of the containing
# function, re-disassembles from the target, and creates a function there. The
# containing function is re-created over what remains before the target, so its
# real body is not lost.
#
# ENV: SPLIT_FUNCS = comma-separated hex addresses
import os
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

monitor = ConsoleTaskMonitor()
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
af = prog.getAddressFactory().getDefaultAddressSpace()

spec = os.environ.get("SPLIT_FUNCS", "").strip()
if not spec:
    print("SPLIT: SPLIT_FUNCS is empty -- split NOTHING")
    raise SystemExit

done = failed = 0
for tok in spec.replace(",", " ").split():
    addr = af.getAddress(tok)
    fn = fm.getFunctionContaining(addr)
    if fn is None:
        print("SPLIT: %s is not inside any function -- use AddFunctions.py" % tok)
        failed += 1
        continue
    start, end = fn.getEntryPoint(), fn.getBody().getMaxAddress()
    if start == addr:
        print("SPLIT: %s is already a function start" % tok)
        continue
    print("SPLIT: %s is inside %s (%s..%s); clearing from the target" %
          (tok, fn.getName(), start, end))
    fm.removeFunction(start)
    listing.clearCodeUnits(addr, end, False)
    DisassembleCommand(addr, None, True).applyTo(prog, monitor)
    if CreateFunctionCmd(addr).applyTo(prog, monitor):
        done += 1
        print("SPLIT: created a function at %s" % tok)
    else:
        failed += 1
        print("SPLIT: FAILED to create a function at %s" % tok)
    # Re-create the earlier function over what is left before the target.
    if start != addr and CreateFunctionCmd(start).applyTo(prog, monitor):
        print("SPLIT: re-created the containing function at %s" % start)
    else:
        print("SPLIT: could NOT re-create the containing function at %s -- its "
              "body is now unattributed" % start)

print("SPLIT: %d split, %d failed, of %d requested"
      % (done, failed, len(spec.replace(",", " ").split())))
