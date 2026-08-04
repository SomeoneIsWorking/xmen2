#@runtime Jython
# Create functions at explicitly given addresses.
#
# The runtime discovers indirect call targets that static analysis never
# resolved into functions -- they have no reference in the database, so
# FillFunctions.py's conservative reference-only filter cannot see them. This
# closes the loop: run, collect the reported addresses, feed them back here,
# re-export, re-recompile.
#
# ENV: ADD_FUNCS = comma-separated hex addresses, e.g. "0x0065adcc,0x00656745"
import os
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

monitor = ConsoleTaskMonitor()
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
af = prog.getAddressFactory().getDefaultAddressSpace()

spec = os.environ.get("ADD_FUNCS", "").strip()
if not spec:
    print("ADD: ADD_FUNCS is empty -- created NOTHING")
    raise SystemExit

wanted = [w.strip() for w in spec.split(",") if w.strip()]
made = existed = failed = 0
for w in wanted:
    a = af.getAddress(long(w, 16))
    if fm.getFunctionContaining(a) is not None:
        print("ADD: 0x%s already inside a function" % w)
        existed += 1
        continue
    if listing.getInstructionAt(a) is None:
        DisassembleCommand(a, None, True).applyTo(prog, monitor)
    if listing.getInstructionAt(a) is None:
        print("ADD: 0x%s did NOT disassemble -- it may be data" % w)
        failed += 1
        continue
    if CreateFunctionCmd(a).applyTo(prog, monitor):
        print("ADD: created function at 0x%s" % w)
        made += 1
    else:
        print("ADD: FAILED to create a function at 0x%s" % w)
        failed += 1

print("ADD: %d created, %d already covered, %d failed, of %d requested"
      % (made, existed, failed, len(wanted)))
if failed:
    print("ADD: failures are NOT harmless -- those addresses stay untranslated "
          "and will keep falling back to original code")
