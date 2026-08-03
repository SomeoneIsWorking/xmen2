#@runtime Jython
from ghidra.program.model.listing import Function
from ghidra.program.model.symbol import RefType

fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
sym = currentProgram.getSymbolTable()

# Find the import symbol DirectInputCreateEx
found = []
for s in sym.getAllSymbols(True):
    if "DirectInputCreateEx" in s.getName():
        found.append(s)

out = []
for s in found:
    refs = s.getReferences()
    out.append("Symbol %s @ %s has %d refs" % (s.getName(), s.getAddress(), len(refs)))
    for r in refs:
        addr = r.getFromAddress()
        fn = fm.getFunctionContaining(addr)
        out.append("  ref from %s in fn %s" % (addr, fn.getName() if fn else "NONE"))

# Also find any calls to CreateDevice / EnumDevices via the imported interface
for it in ["CreateDeviceA", "CreateDeviceW", "EnumDevicesA", "EnumDevicesW",
           "GetDeviceState", "SetDataFormat", "Poll", "Acquire", "Unacquire"]:
    for s in sym.getAllSymbols(True):
        if s.getName() == it or (it + "@") in s.getName():
            refs = s.getReferences()
            for r in refs:
                addr = r.getFromAddress()
                fn = fm.getFunctionContaining(addr)
                out.append("%s: ref from %s in fn %s" % (it, addr, fn.getName() if fn else "NONE"))

with open("/path/to/X-Men Legends II/scratch/logs/dinput_xrefs.txt", "w") as f:
    f.write("\n".join(out))
print("\n".join(out[:60]))
