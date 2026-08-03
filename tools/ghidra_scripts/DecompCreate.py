#@runtime Jython
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

ifc = DecompInterface()
ifc.setOptions(DecompileOptions()); ifc.toggleCCode(True); ifc.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

addr = currentProgram.getAddressFactory().getDefaultAddressSpace()
targets = [0x100052a0, 0x10005130, 0x100051f0, 0x10004840]
out = []
for a in targets:
    fn = currentProgram.getFunctionManager().getFunctionAt(addr.getAddress(a))
    if not fn:
        out.append("NO FN @ %x" % a); continue
    res = ifc.decompileFunction(fn, 60, monitor)
    out.append("===== %s @ %s =====" % (fn.getName(), fn.getEntryPoint()))
    out.append(res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAIL "+str(res.getErrorMessage()))

with open("/path/to/X-Men Legends II/scratch/logs/createControllers.txt", "w") as f:
    f.write("\n".join(out))
print("done")
