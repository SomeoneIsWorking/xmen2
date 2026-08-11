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

# The DATA guard, and why it is not optional.
#
# The seeds used to come only from the runtime, where an address that was
# CALLED is code by definition. tools/seed_relocs.py enumerates every absolute
# code pointer in the image instead, which is complete but not selective: MSVC
# puts read-only tables and string literals in .text for a DLL with no .rdata
# section, and a pointer to one of those relocates exactly like a function
# pointer. x86 will happily decode a string, so "it disassembled" is not the
# test -- Ghidra's own code/data separation is, and that is what this asks.
#
# Anything rejected here is COUNTED and the reason is named. A seeding pass
# that quietly dropped half its input would look identical to one that had
# nothing to do.
wanted = [w.strip() for w in spec.split(",") if w.strip()]
made = existed = failed = isdata = 0
SHOW = 12                      # per-outcome chatter; failures are never capped
for w in wanted:
    a = af.getAddress(long(w, 16))
    if fm.getFunctionContaining(a) is not None:
        if existed < SHOW:
            print("ADD: 0x%s already inside a function" % w)
        existed += 1
        continue
    d = listing.getDataContaining(a)
    if d is not None and d.isDefined():
        if isdata < SHOW:
            print("ADD: 0x%s is inside DEFINED DATA (%s) -- not seeded"
                  % (w, d.getDataType().getName()))
        isdata += 1
        continue
    if listing.getInstructionAt(a) is None:
        DisassembleCommand(a, None, True).applyTo(prog, monitor)
    if listing.getInstructionAt(a) is None:
        print("ADD: 0x%s did NOT disassemble -- it may be data" % w)
        failed += 1
        continue
    if CreateFunctionCmd(a).applyTo(prog, monitor):
        if made < SHOW:
            print("ADD: created function at 0x%s" % w)
        made += 1
    else:
        print("ADD: FAILED to create a function at 0x%s" % w)
        failed += 1

print("ADD: %d created, %d already covered, %d rejected as defined data, "
      "%d failed, of %d requested"
      % (made, existed, isdata, failed, len(wanted)))
if made > SHOW or existed > SHOW or isdata > SHOW:
    print("ADD: per-address lines were capped at %d each; the counts above are "
          "the whole set" % SHOW)
if failed:
    print("ADD: failures are NOT harmless -- those addresses stay untranslated "
          "and will keep falling back to original code")
