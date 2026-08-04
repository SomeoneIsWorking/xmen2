#@runtime Jython
# Raise function coverage by disassembling undefined bytes in executable
# sections and promoting orphan instruction runs into functions.
#
# Why: the recompiler can only translate what Ghidra identified as a function.
# 22% of XMen2.exe's executable bytes sat in no function, and at runtime an
# indirect call into that region has nothing to dispatch to. Rather than adding
# addresses one at a time as each run stops, sweep them.
#
# This is deliberately conservative about what it calls code: it only
# disassembles at addresses that are already the target of a reference, or that
# follow a run of defined instructions, because blindly disassembling data
# manufactures functions full of nonsense that then translate "successfully".
import os
from ghidra.program.model.address import AddressSet
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

monitor = ConsoleTaskMonitor()
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
mem = prog.getMemory()
rm = prog.getReferenceManager()

exec_set = AddressSet()
for blk in mem.getBlocks():
    if blk.isExecute() and blk.isInitialized():
        exec_set.addRange(blk.getStart(), blk.getEnd())

def covered_bytes():
    s = AddressSet()
    for fn in fm.getFunctions(True):
        s.add(fn.getBody())
    return s.getNumAddresses()

before_fns = fm.getFunctionCount()
before_cov = covered_bytes()
total_exec = exec_set.getNumAddresses()
print("FILL: start -- %d functions, %d of %d executable bytes covered (%.1f%%)"
      % (before_fns, before_cov, total_exec, 100.0 * before_cov / total_exec))

# ---- 1. disassemble at referenced addresses that are still undefined
targets = []
for addr in exec_set.getAddresses(True):
    if listing.getInstructionAt(addr) is not None:
        continue
    if listing.getDefinedDataAt(addr) is not None:
        continue
    refs = rm.getReferencesTo(addr)
    if not refs.hasNext():
        continue
    for r in refs:
        rt = r.getReferenceType()
        if rt.isCall() or rt.isJump() or rt.isIndirect():
            targets.append(addr)
            break

print("FILL: %d referenced-but-undefined addresses to disassemble" % len(targets))
dis_ok = 0
for a in targets:
    cmd = DisassembleCommand(a, None, True)
    if cmd.applyTo(prog, monitor):
        dis_ok += 1
print("FILL: disassembled %d of %d" % (dis_ok, len(targets)))

# ---- 2. promote instruction runs that belong to no function
made = 0
tried = 0
for addr in exec_set.getAddresses(True):
    ins = listing.getInstructionAt(addr)
    if ins is None:
        continue
    if fm.getFunctionContaining(addr) is not None:
        continue
    # only start a function where something actually calls or jumps here
    refs = rm.getReferencesTo(addr)
    is_target = False
    for r in refs:
        rt = r.getReferenceType()
        if rt.isCall() or rt.isJump() or rt.isIndirect():
            is_target = True
            break
    if not is_target:
        continue
    tried += 1
    cmd = CreateFunctionCmd(addr)
    if cmd.applyTo(prog, monitor):
        made += 1

after_fns = fm.getFunctionCount()
after_cov = covered_bytes()
print("FILL: created %d functions from %d candidate entry points" % (made, tried))
print("FILL: end -- %d functions (+%d), %d of %d executable bytes covered "
      "(%.1f%%, was %.1f%%)"
      % (after_fns, after_fns - before_fns, after_cov, total_exec,
         100.0 * after_cov / total_exec, 100.0 * before_cov / total_exec))
if after_cov == before_cov:
    print("FILL: coverage did NOT change -- either everything reachable is "
          "already a function, or the reference-only filter rejected every "
          "candidate. This is not evidence that the remaining bytes are data.")
