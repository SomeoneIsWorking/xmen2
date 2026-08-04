#@runtime Jython
# Decompile a function by (demangled) name. ENV: DECOMP_NAME=<substring>
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

want = os.environ.get("DECOMP_NAME", "")
out_path = os.environ.get("DECOMP_OUT", "scratch/logs/decomp.txt")

prog = currentProgram
fm = prog.getFunctionManager()
monitor = ConsoleTaskMonitor()

targets = []
for fn in fm.getFunctions(True):
    nm = fn.getName()
    if want and want in nm:
        targets.append(fn)
if not targets:
    print("NO_MATCH " + want)
    raise SystemExit

decomp = DecompInterface()
decomp.openProgram(prog)
lines = []
for fn in targets:
    res = decomp.decompileFunction(fn, 60, monitor)
    body = res.getDecompiledFunction().getC()
    lines.append("/*===== %s @ %s =====*/\n%s\n" % (fn.getName(), fn.getEntryPoint(), body))
print("WROTE %d funcs" % len(targets))
print(lines[0][:2000])
with open(out_path, "w") as fp:
    fp.write("\n".join(lines))
