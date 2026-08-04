#@runtime Jython
import os
want = os.environ.get("FIND_NAME", "")
prog = currentProgram
symt = prog.getSymbolTable()
fms = prog.getFunctionManager()
out = []
for s in symt.getAllSymbols(True):
    nm = s.getName()
    if want.lower() in nm.lower() and not nm.startswith("_DAT_"):
        fn = fms.getFunctionAt(s.getAddress())
        out.append("0x%08x %s" % (s.getAddress().getOffset(), nm))
for line in sorted(out):
    print(line)
