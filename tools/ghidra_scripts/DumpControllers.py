#@runtime Jython
from x2out import outpath
from ghidra.program.model.listing import Function
from ghidra.program.model.address import AddressSet

fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()

targets = [
    "igWin32ControllerManager",
    "igWin32Controller",
    "igControllerManager",
    "igController",
    "igControllerList",
    "igControllerStack",
    "igControllerStack",
]

# Find functions by name (demangled won't match, so search mangled too)
found = []
for fn in fm.getFunctions(True):
    n = fn.getName()
    if any(t in n for t in targets):
        found.append(fn)

out = []
for fn in found:
    body = fn.getBody()
    out.append("### %s @ %s (%d bytes)" % (fn.getName(), fn.getEntryPoint(), body.getNumAddresses()))

with open(outpath("controllers_funcs.txt"), "w") as f:
    f.write("\n".join(out))
print("found %d functions" % len(found))
for fn in found:
    print(fn.getName(), fn.getEntryPoint())
