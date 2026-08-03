#@runtime Jython
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

ifc = DecompInterface()
ifc.setOptions(DecompileOptions())
ifc.toggleCCode(True)
ifc.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# decompile the full igWin32Controller (185 bytes) at 100029e0 and the manager 100026c0
targets = [0x100029e0, 0x100026c0, 0x10002aa0, 0x100050b0]
out = []
for addr in targets:
    fn = currentProgram.getFunctionManager().getFunctionAt(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addr))
    if fn is None:
        out.append("===== NO FN @ %x =====" % addr)
        continue
    res = ifc.decompileFunction(fn, 60, monitor)
    out.append("===== %s @ %s =====" % (fn.getName(), fn.getEntryPoint()))
    out.append(res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAILED: "+str(res.getErrorMessage()))

with open("/path/to/X-Men Legends II/scratch/logs/controller_decomp.txt", "w") as f:
    f.write("\n".join(out))
print("done")
