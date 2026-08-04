#@runtime Jython
# Decompile functions by hex address list. ENV: DECOMP_ADDRS=0x1007fde0,0x1007f610
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

addrs = [int(a, 16) for a in os.environ.get("DECOMP_ADDRS", "").split(",") if a.strip()]
out_path = os.environ.get("DECOMP_OUT", "scratch/logs/decomp.txt")

prog = currentProgram
fm = prog.getFunctionManager()
monitor = ConsoleTaskMonitor()
af = prog.getAddressFactory()

decomp = DecompInterface()
decomp.openProgram(prog)
lines = []
for a in addrs:
    fn = fm.getFunctionAt(af.getDefaultAddressSpace().getAddress(a))
    if fn is None:
        lines.append("/*===== 0x%x : NO FUNCTION =====*/\n" % a)
        continue
    res = decomp.decompileFunction(fn, 60, monitor)
    body = res.getDecompiledFunction().getC()
    lines.append("/*===== %s @ 0x%x =====*/\n%s\n" % (fn.getName(), a, body))
with open(out_path, "w") as fp:
    fp.write("\n".join(lines))
print("WROTE %d" % len(addrs))
