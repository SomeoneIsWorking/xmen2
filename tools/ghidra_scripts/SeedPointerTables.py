#@runtime Jython
# Create functions from RUNS of code pointers in read-only data -- vtables and
# dispatch tables.
#
# Why in bulk. The runtime finds these one at a time: an indirect call reaches
# an address with no recompiled body, execution stops, the loop seeds that ONE
# address and rebuilds. For virtual calls that is the wrong shape entirely --
# there are thousands of them, each costing a Ghidra export and a rebuild, and
# the run only advances by one function per round. The tables that hold them
# can be enumerated statically instead.
#
# What makes a run rather than a coincidence. A single dword that happens to
# look like a .text address is common; three or more CONSECUTIVE aligned dwords
# all pointing into .text is what a vtable looks like and what stray constants
# do not. The threshold is deliberately conservative -- this creates functions,
# and a wrong one is a body full of garbage that will be dispatched to.
#
# What it will NOT do:
#   - split an address that is already inside a function (that needs
#     SplitFunction.py, and doing it blind here would wreck good analysis)
#   - touch a target that is already a function start
#   - accept a target outside the executable sections
#
# ENV:
#   SEED_MIN_RUN   consecutive pointers required (default 3)
#   SEED_MAX       stop after creating this many (default 0 = no limit)
import os
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

monitor = ConsoleTaskMonitor()
prog = currentProgram
mem = prog.getMemory()
fm = prog.getFunctionManager()
af = prog.getAddressFactory().getDefaultAddressSpace()

MIN_RUN = int(os.environ.get("SEED_MIN_RUN", "3"))
MAX_NEW = int(os.environ.get("SEED_MAX", "0"))

exec_ranges = []
scan_blocks = []
for b in mem.getBlocks():
    if not b.isInitialized():
        continue
    if b.isExecute():
        exec_ranges.append((b.getStart().getOffset(), b.getEnd().getOffset()))
    elif b.isRead() and not b.isWrite():
        scan_blocks.append(b)          # .rdata: where vtables live

if not exec_ranges:
    print("SEED: no executable blocks -- scanned NOTHING")
    raise SystemExit
if not scan_blocks:
    print("SEED: no read-only data blocks -- scanned NOTHING, which is not the "
          "same as finding nothing")
    raise SystemExit


def is_code(v):
    for lo, hi in exec_ranges:
        if lo <= v <= hi:
            return True
    return False


scanned = runs = cand = already = created = failed = inside = 0

for b in scan_blocks:
    start, end = b.getStart().getOffset(), b.getEnd().getOffset()
    a = (start + 3) & ~3
    run = []
    while a + 4 <= end:
        scanned += 1
        try:
            v = mem.getInt(af.getAddress(a)) & 0xFFFFFFFF
        except:
            v = 0
        if is_code(v):
            run.append(v)
        else:
            if len(run) >= MIN_RUN:
                runs += 1
                for t in run:
                    cand += 1
                    ta = af.getAddress(t)
                    if fm.getFunctionAt(ta) is not None:
                        already += 1
                        continue
                    if fm.getFunctionContaining(ta) is not None:
                        inside += 1        # needs a split, not a create
                        continue
                    DisassembleCommand(ta, None, True).applyTo(prog, monitor)
                    if CreateFunctionCmd(ta).applyTo(prog, monitor):
                        created += 1
                        if MAX_NEW and created >= MAX_NEW:
                            print("SEED: hit SEED_MAX=%d, stopping early -- "
                                  "this is a CAP, not a conclusion" % MAX_NEW)
                            a = end
                            break
                    else:
                        failed += 1
            run = []
        a += 4
    if len(run) >= MIN_RUN:
        runs += 1

# Every number, every time. "created 0" and "scanned 0" mean completely
# different things and must never print the same way.
print("SEED: scanned %d aligned dwords in %d read-only block(s)"
      % (scanned, len(scan_blocks)))
print("SEED: %d run(s) of >=%d consecutive code pointers, %d target(s) in them"
      % (runs, MIN_RUN, cand))
print("SEED: %d already functions, %d inside an existing function (need a "
      "SPLIT, not a create), %d created, %d failed"
      % (already, inside, created, failed))
