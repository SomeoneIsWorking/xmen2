#@runtime Jython
# Read the ControllerType enum string table at 10021984 / 10021990
addr = currentProgram.getAddressFactory().getDefaultAddressSpace()
a = addr.getAddress(0x10021984)
# Read up to 3 pointers and following strings
from ghidra.program.model.address import Address
out = []
mem = currentProgram.getMemory()
import struct
for i in range(8):
    p = a.add(i*4)
    try:
        b = mem.getBytes(p, 4)
        val = struct.unpack('<I', b)[0]
    except Exception as e:
        out.append("fail @ %s: %s" % (p, e)); break
    out.append("ptr[%d] @ %s = 0x%x" % (i, p, val))
with open("/path/to/X-Men Legends II/scratch/logs/enum_ptrs.txt", "w") as f:
    f.write("\n".join(out))
print("\n".join(out))
