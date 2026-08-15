#@runtime Jython
# Repair a function whose ENTRY has no instruction: a 1-byte stub standing in
# front of a misaligned decode.
#
# The shape, measured on XMen2.exe 0x00424240 (issue #76):
#
#   0x00424240  size 1, ZERO instructions   <- the real entry, from a vtable
#   0x00424242  size 363, 102 instructions  <- a function starting two bytes in
#
# The real function begins `sub esp,0x14; push esi; mov esi,ecx`. Ghidra
# decoded the region from 0x00424242 first -- reading the tail of that prologue
# as `ADC AL,0x56` and then re-synchronising -- so when the address was later
# seeded from its vtable slot there was one byte of room in front of an
# existing function, and the seed became a stub with nothing in it. The
# recompiler translates a stub into a trap, and the trap fires whenever the
# game makes that virtual call: a crash minutes into gameplay, with nothing to
# connect it to a disassembly that went wrong.
#
# SplitFunction.py cannot fix this. It repairs the opposite case -- an address
# swallowed INSIDE a function -- and bails here with "already a function start",
# because the stub IS a function start. What is wrong is the function AFTER it.
#
# So: remove the stub and every function that begins inside the region it
# should own, clear the code units across it, disassemble from the true entry,
# and create the function there. Any removed neighbour whose entry is still an
# instruction boundary afterwards is re-created, so a genuine function that
# merely followed the stub is not lost.
#
# ENV:
#   REPAIR_FUNCS = comma-separated hex addresses, or
#   REPAIR_ALL=1 = scan every function and repair each one with no instruction
#                  at its entry
#   REPAIR_SPAN  = how far past the entry a misaligned neighbour may start and
#                  still be treated as part of this function (default 16)
import os
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

monitor = ConsoleTaskMonitor()
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
af = prog.getAddressFactory().getDefaultAddressSpace()

SPAN = int(os.environ.get("REPAIR_SPAN", "16"))

# ---- what to repair, and SAY the denominator either way -------------------
#
# "Nothing to repair" and "I never looked" have to be different sentences. The
# scan prints how many functions it examined before saying none were broken.
spec = os.environ.get("REPAIR_FUNCS", "").strip()
targets = []
if os.environ.get("REPAIR_ALL", "").strip() not in ("", "0"):
    scanned = 0
    for fn in fm.getFunctions(True):
        scanned += 1
        if listing.getInstructionAt(fn.getEntryPoint()) is None:
            targets.append(fn.getEntryPoint())
    print("REPAIR: scanned %d function(s); %d have NO instruction at their "
          "entry" % (scanned, len(targets)))
elif spec:
    for tok in spec.replace(",", " ").split():
        targets.append(af.getAddress(tok))
    print("REPAIR: %d address(es) named by REPAIR_FUNCS" % len(targets))
else:
    print("REPAIR: neither REPAIR_FUNCS nor REPAIR_ALL is set -- repaired "
          "NOTHING, and looked at nothing")
    raise SystemExit

done = skipped = failed = removed = 0
for addr in targets:
    if listing.getInstructionAt(addr) is not None:
        print("REPAIR: %s already decodes to an instruction -- nothing to do"
              % addr)
        skipped += 1
        continue

    # Everything that begins inside the span this entry should own. The stub
    # itself is first; a misaligned neighbour is what actually holds the bytes.
    victims = []
    end = addr.add(SPAN - 1)
    fn = fm.getFunctionAt(addr)
    if fn is not None:
        victims.append(addr)
    probe = fm.getFunctionsOverlapping(
        prog.getAddressFactory().getAddressSet(addr, end))
    for other in probe:
        ep = other.getEntryPoint()
        if ep.equals(addr):
            continue
        if ep.compareTo(addr) > 0 and ep.compareTo(end) <= 0:
            victims.append(ep)
            body_max = other.getBody().getMaxAddress()
            if body_max.compareTo(end) > 0:
                end = body_max

    print("REPAIR: %s has no instruction; clearing %s..%s and removing %d "
          "function(s): %s"
          % (addr, addr, end, len(victims),
             ", ".join([str(v) for v in victims])))
    for v in victims:
        fm.removeFunction(v)
    listing.clearCodeUnits(addr, end, False)
    DisassembleCommand(addr, None, True).applyTo(prog, monitor)

    if listing.getInstructionAt(addr) is None:
        # An address that will not disassemble is not a function, and leaving a
        # function record over it is the worse of the two outcomes: the export
        # then carries a body whose code starts PAST its own entry (0x0065006c
        # came back with two instructions 17 bytes in), which reads as repaired
        # and is not. Remove it. The runtime then reports an unknown call
        # target if anything ever reaches it -- a missing-target line naming
        # the address, instead of a trap claiming a function exists there.
        if fm.getFunctionAt(addr) is not None:
            fm.removeFunction(addr)
        print("REPAIR: %s does NOT disassemble -- it is data or a bogus seed, "
              "not a function; the function record is REMOVED" % addr)
        removed += 1
        continue
    if CreateFunctionCmd(addr).applyTo(prog, monitor):
        fn = fm.getFunctionAt(addr)
        n = 0
        for _ in listing.getInstructions(fn.getBody(), True):
            n += 1
        print("REPAIR: created a function at %s -- %d instruction(s), %d byte(s)"
              % (addr, n, fn.getBody().getNumAddresses()))
        done += 1
    else:
        print("REPAIR: FAILED to create a function at %s although it "
              "disassembled" % addr)
        failed += 1
        continue

    # A neighbour that is still an instruction boundary was a real function
    # that merely followed the stub; put it back rather than leaving its body
    # attributed to the repaired one.
    for v in victims:
        if v.equals(addr):
            continue
        if listing.getInstructionAt(v) is not None and fm.getFunctionAt(v) is None:
            if CreateFunctionCmd(v).applyTo(prog, monitor):
                print("REPAIR:   re-created the following function at %s" % v)
            else:
                print("REPAIR:   %s is an instruction boundary but no function "
                      "could be made there" % v)
        else:
            print("REPAIR:   %s is NOT an instruction boundary after the "
                  "repair -- it was a misaligned decode, and is gone" % v)

print("REPAIR: %d repaired, %d already fine, %d removed as not-code, %d "
      "FAILED, of %d considered"
      % (done, skipped, failed, removed, len(targets)))
