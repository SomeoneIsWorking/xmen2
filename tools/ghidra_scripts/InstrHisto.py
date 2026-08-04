#@runtime Jython
# Instruction histogram over IDENTIFIED FUNCTION BODIES only, plus how much of
# the executable sections those bodies actually cover.
#
# Why not objdump: a linear sweep of .text decodes padding and data as code
# (it reported 29% `nop` and 146 `aas` for libIGDisplay -- both artefacts), so
# it cannot size a recompiler's decoder job. Ghidra's function bodies come from
# recursive descent off real entry points.
#
# The coverage number is the point: instructions we can enumerate are the ones a
# recompiler can translate. Bytes in an executable section that belong to NO
# identified function are the unsolved part, and this prints them rather than
# quietly reporting only what it found.
#
# ENV: HISTO_OUT (default scratch/logs/histo.txt)
import os

out_path = os.environ.get("HISTO_OUT", "scratch/logs/histo.txt")
prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()

# executable byte total
exec_bytes = 0
for blk in prog.getMemory().getBlocks():
    if blk.isExecute():
        exec_bytes += blk.getSize()

counts = {}
total = 0
body_bytes = 0
nfunc = 0
thunks = 0
for fn in fm.getFunctions(True):
    nfunc += 1
    if fn.isThunk():
        thunks += 1
    body = fn.getBody()
    body_bytes += body.getNumAddresses()
    for ins in listing.getInstructions(body, True):
        m = ins.getMnemonicString()
        counts[m] = counts.get(m, 0) + 1
        total += 1

items = sorted(counts.items(), key=lambda kv: -kv[1])
lines = []
lines.append("program: %s" % prog.getName())
lines.append("executable bytes in image : %d" % exec_bytes)
lines.append("bytes inside identified fns: %d (%.1f%% of executable bytes)"
             % (body_bytes, 100.0 * body_bytes / exec_bytes if exec_bytes else 0))
lines.append("bytes NOT in any identified function: %d  <-- the part a "
             "recompiler cannot yet see" % (exec_bytes - body_bytes))
lines.append("functions: %d (of which thunks: %d)" % (nfunc, thunks))
lines.append("instructions decoded: %d   distinct mnemonics: %d"
             % (total, len(items)))
lines.append("")

run = 0
marks = [10, 20, 30, 50, 80, 120, 200]
lines.append("%-14s %8s %7s %7s" % ("MNEMONIC", "COUNT", "PCT", "CUM"))
for i, (m, c) in enumerate(items):
    run += c
    lines.append("%-14s %8d %6.2f%% %6.2f%%"
                 % (m, c, 100.0 * c / total, 100.0 * run / total))
lines.append("")
run = 0
for i, (m, c) in enumerate(items):
    run += c
    if (i + 1) in marks:
        lines.append("top-%-4d covers %.2f%% of instructions" % (i + 1, 100.0 * run / total))
lines.append("mnemonics appearing exactly once: %d"
             % len([1 for m, c in items if c == 1]))

d = os.path.dirname(out_path)
if d and not os.path.isdir(d):
    os.makedirs(d)
f = open(out_path, "w")
f.write("\n".join(lines))
f.close()
print("HISTO: %s -- %d instrs, %d mnemonics, %.1f%% of exec bytes covered by "
      "%d functions -> %s"
      % (prog.getName(), total, len(items),
         100.0 * body_bytes / exec_bytes if exec_bytes else 0, nfunc, out_path))
