#@runtime Jython
from x2out import outpath
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

ifc = DecompInterface()
ifc.setOptions(DecompileOptions()); ifc.toggleCCode(True); ifc.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

names = ["getButtonState", "getButtonPressure", "getButtonsState", "getNativeJoystickState",
         "setEventsFunction", "getEvents"]
out = []
for fn in currentProgram.getFunctionManager().getFunctions(True):
    if fn.getName() in names:
        res = ifc.decompileFunction(fn, 60, monitor)
        out.append("===== %s @ %s =====" % (fn.getName(), fn.getEntryPoint()))
        out.append(res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAIL "+str(res.getErrorMessage()))

# Also find functions referencing _buttonMap symbol
out.append("\n===== functions referencing _buttonMap =====")
sym = currentProgram.getSymbolTable()
for s in sym.getAllSymbols(True):
    if "_buttonMap" in s.getName():
        for ref in currentProgram.getReferenceManager().getReferencesTo(s.getAddress()):
            fn = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress())
            if fn:
                out.append("  %s @ %s refs _buttonMap" % (fn.getName(), fn.getEntryPoint()))
            else:
                out.append("  NONE @ %s refs _buttonMap" % ref.getFromAddress())

with open(outpath("buttonmap.txt"), "w") as f:
    f.write("\n".join(out))
print("done")
