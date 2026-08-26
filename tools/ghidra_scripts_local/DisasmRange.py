#@runtime Jython
# Print instructions in [start, end). ENV: DISASM_START=0x..., DISASM_END=0x...
import os
from ghidra.program.model.address import AddressSet
prog = currentProgram
listing = prog.getListing()
af = prog.getAddressFactory().getDefaultAddressSpace()
start = af.getAddress(int(os.environ["DISASM_START"], 16))
end = af.getAddress(int(os.environ["DISASM_END"], 16))
it = listing.getInstructions(AddressSet(start, end), True)
for ins in it:
    print("%s  %s" % (ins.getAddress(), ins))
