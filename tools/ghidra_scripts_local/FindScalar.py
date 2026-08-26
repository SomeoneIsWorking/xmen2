#@runtime Jython
# Every function whose instructions contain a given scalar operand.
#
# What a NEGATIVE prints: "scanned N instruction(s) in M function(s), 0 carried
# the scalar" -- so "no hits" can never be confused with "never looked".
import os
from ghidra.program.model.lang import OperandType

want = long(os.environ["X2_SCALAR"], 16)
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
hits = {}
ninstr = 0
nfunc = 0
for fn in fm.getFunctions(True):
    nfunc += 1
    for instr in listing.getInstructions(fn.getBody(), True):
        ninstr += 1
        for i in range(instr.getNumOperands()):
            for obj in instr.getOpObjects(i):
                try:
                    v = obj.getValue()
                except Exception:
                    continue
                if long(v) == want:
                    hits.setdefault(fn.getName(), []).append(
                        "%s  %s" % (instr.getAddress(), instr))
for name in sorted(hits):
    print("SCALAR %s (%d)" % (name, len(hits[name])))
    for line in hits[name][:6]:
        print("    " + line)
print("SCALAR scanned %d instruction(s) in %d function(s); %d function(s) "
      "carried 0x%x" % (ninstr, nfunc, len(hits), want))
