#@runtime Jython
from x2out import outpath
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

ifc = DecompInterface()
ifc.setOptions(DecompileOptions())
ifc.toggleCCode(True)
ifc.openProgram(currentProgram)

monitor = ConsoleTaskMonitor()
targets = ["initialize", "userConstruct"]

out = []
for fn in currentProgram.getFunctionManager().getFunctions(True):
    if fn.getName() in targets:
        res = ifc.decompileFunction(fn, 60, monitor)
        out.append("===== %s @ %s =====" % (fn.getName(), fn.getEntryPoint()))
        if res.decompileCompleted():
            out.append(res.getDecompiledFunction().getC())
        else:
            out.append("DECOMP FAILED: " + str(res.getErrorMessage()))

with open(outpath("init_decomp.txt"), "w") as f:
    f.write("\n".join(out))
print("wrote %d chars" % sum(len(x) for x in out))
