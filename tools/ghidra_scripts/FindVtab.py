#@runtime Jython
import os
want = os.environ.get("VTAB_NAME", "PgAnimationStreamImp::vftable")
prog = currentProgram
symt = prog.getSymbolTable()
fms = prog.getFunctionManager(); af = prog.getAddressFactory(); mem = prog.getMemory()
for s in symt.getAllSymbols(False):
    nm = s.getName()
    if want.lower() in nm.lower():
        a = s.getAddress()
        print("SYM %s @ %s" % (nm, a))
        for i in range(0x50//4):
            b = mem.getInt(a.add(i*4))
            fn = fms.getFunctionAt(af.getDefaultAddressSpace().getAddress(b))
            print("  +%02x -> 0x%08x %s" % (i*4, b, fn.getName() if fn else ""))
        break
