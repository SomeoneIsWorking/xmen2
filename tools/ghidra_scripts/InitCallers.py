#@runtime Jython
from ghidra.program.model.listing import Function
from ghidra.program.model.symbol import RefType

fm = currentProgram.getFunctionManager()

# callers of initializeControllers
for fn in fm.getFunctions(True):
    if fn.getName() == "initializeControllers":
        for ref in currentProgram.getReferenceManager().getReferencesTo(fn.getEntryPoint()):
            caller = fm.getFunctionContaining(ref.getFromAddress())
            print("initializeControllers called from %s @ %s" % (caller.getName() if caller else "NONE", ref.getFromAddress()))
        print("----")

# find getEventsFunction body - it processes DIJOYSTATE
for fn in fm.getFunctions(True):
    if "getEventsFunction" in fn.getName():
        print(fn.getName(), fn.getEntryPoint(), fn.getBody().getNumAddresses())
