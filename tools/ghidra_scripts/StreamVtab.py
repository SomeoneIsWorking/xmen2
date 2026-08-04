#@runtime Jython
prog = currentProgram; fms = prog.getFunctionManager(); af = prog.getAddressFactory(); mem = prog.getMemory()
fn = fms.getFunctionAt(af.getDefaultAddressSpace().getAddress(0x100809b0))
ls = prog.getListing()
inst = ls.getInstructions(fn.getBody(), True)
for i in inst:
    scalars = i.getScalars(0)
    for s in scalars:
        v = s.getValue()
        if v > 0x10000000:
            print("MOV const 0x%x" % v)
            n = 8
            a = af.getDefaultAddressSpace().getAddress(v)
            for j in range(n):
                b = mem.getInt(a.add(j*4))
                print("  vtbl+%02x -> 0x%08x" % (j*4, b))
