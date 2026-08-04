#@runtime Jython
import os
addr = int(os.environ.get("VTAB", "0"), 16)
n = int(os.environ.get("VTAB_N", "20"), 16)
prog = currentProgram; af = prog.getAddressFactory(); mem = prog.getMemory()
a = af.getDefaultAddressSpace().getAddress(addr)
for i in range(n):
    b = mem.getInt(a.add(i*4))
    print("vtbl+%02x -> 0x%08x" % (i*4, b))
