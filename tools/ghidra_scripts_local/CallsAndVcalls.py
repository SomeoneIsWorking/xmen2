#@runtime Jython
# For every function that CALLs a target VA, list the virtual-call offsets it
# also uses. ENV: X2_TARGET=0x597c20 [, X2_VOFF=0x8,0x1c]
#
# Negative print: names how many callers were examined, so "no function uses
# those offsets" cannot be confused with "no callers were found".
import os
target = int(os.environ["X2_TARGET"], 16)
want = [int(v, 16) for v in os.environ.get("X2_VOFF", "").split(",") if v.strip()]
prog = currentProgram
af = prog.getAddressFactory().getDefaultAddressSpace()
fm = prog.getFunctionManager()
listing = prog.getListing()
callers = set()
for r in prog.getReferenceManager().getReferencesTo(af.getAddress(target)):
    fn = fm.getFunctionContaining(r.getFromAddress())
    if fn:
        callers.add(fn)
hit = 0
for fn in sorted(callers, key=lambda f: f.getEntryPoint().getOffset()):
    offs = []
    for instr in listing.getInstructions(fn.getBody(), True):
        if instr.getMnemonicString() != "CALL":
            continue
        rep = instr.toString()
        if "dword ptr [" not in rep:
            continue
        offs.append(rep)
    picked = [o for o in offs if not want or any(
        ("+ 0x%x]" % w) in o for w in want)]
    if picked:
        hit += 1
        print("FN %s @ %s" % (fn.getName(), fn.getEntryPoint()))
        for o in sorted(set(picked)):
            print("    " + o)
print("VCALL %d of %d caller(s) of 0x%x used offset(s) %s"
      % (hit, len(callers), target, want or "any"))
