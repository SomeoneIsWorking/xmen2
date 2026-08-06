#@runtime Jython
from x2out import outpath
from ghidra.program.model.listing import Function
from ghidra.program.model.symbol import SourceType

fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()

# Find the function getBUTTONSMetaEnum and follow to the enum data
found = None
for fn in fm.getFunctions(True):
    if "getBUTTONSMetaEnum" in fn.getName():
        found = fn
        break

out = []
if found:
    out.append("getBUTTONSMetaEnum @ %s" % found.getEntryPoint())
    # decompile
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.util.task import ConsoleTaskMonitor
    ifc = DecompInterface()
    ifc.setOptions(DecompileOptions()); ifc.toggleCCode(True); ifc.openProgram(currentProgram)
    res = ifc.decompileFunction(found, 60, ConsoleTaskMonitor())
    out.append(res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAIL")

# Search all data for igMetaEnum of BUTTONS - look at strings near 'BUTTONS'
# Simpler: dump any function in igController that references button constants
out.append("\n==== igController functions ====")
for fn in fm.getFunctions(True):
    n = fn.getName()
    if "igController" in n or "Controller" in n:
        out.append("%s @ %s (%d)" % (n, fn.getEntryPoint(), fn.getBody().getNumAddresses()))

with open(outpath("buttons_enum.txt"), "w") as f:
    f.write("\n".join(out))
print("\n".join(out))
