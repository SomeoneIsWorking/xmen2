#@runtime Jython
# Decompile the ARK (Alchemy meta-object registration) entry points and, for
# each, list the EXTERNAL functions it calls -- that external list is the real
# deliverable: it is the libIGCore API a replacement DLL has to speak.
#
# ENV:
#   ARK_PAT   comma-separated substrings to match against mangled names
#             (default: the ARK entry points)
#   ARK_OUT   output path (default scratch/logs/ark.txt)
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

pats = [p for p in os.environ.get(
    "ARK_PAT",
    "arkRegister,getClassTypeLazy,_instantiateFromPool,retrieveVTablePointer"
).split(",") if p]
out_path = os.environ.get("ARK_OUT", "scratch/logs/ark.txt")

prog = currentProgram
fm = prog.getFunctionManager()
monitor = ConsoleTaskMonitor()

scanned = 0
targets = []
for fn in fm.getFunctions(True):
    scanned += 1
    nm = fn.getName()
    for p in pats:
        if p in nm:
            targets.append(fn)
            break

print("ARK: program=%s scanned=%d functions, matched=%d for patterns %s"
      % (prog.getName(), scanned, len(targets), pats))
if not targets:
    # A negative must carry its denominator, not just print nothing.
    print("ARK: NO MATCHES among %d functions -- the patterns matched NOTHING, "
          "which is not the same as 'these functions do not exist'. Check that "
          "the right program is loaded and that symbols are demangled." % scanned)
    raise SystemExit

decomp = DecompInterface()
decomp.openProgram(prog)

listing = prog.getListing()
lines = []
lines.append("program: %s" % prog.getName())
lines.append("scanned %d functions, %d matched patterns %s\n"
             % (scanned, len(targets), pats))

for fn in sorted(targets, key=lambda f: f.getName()):
    lines.append("/*===== %s @ %s =====*/" % (fn.getName(), fn.getEntryPoint()))
    # external calls first -- the API surface we must reimplement against
    ext = []
    for callee in fn.getCalledFunctions(monitor):
        if callee.isExternal() or callee.isThunk():
            tgt = callee
            if callee.isThunk():
                t = callee.getThunkedFunction(True)
                if t is not None:
                    tgt = t
            lib = ""
            try:
                el = tgt.getExternalLocation()
                if el is not None:
                    lib = el.getLibraryName() + "!"
            except Exception:
                pass
            ext.append(lib + tgt.getName())
    if ext:
        lines.append("// EXTERNAL CALLS (%d): %s" % (len(ext), ", ".join(sorted(set(ext)))))
    else:
        lines.append("// EXTERNAL CALLS: none found -- either this function is "
                     "self-contained or Ghidra did not resolve its imports")
    res = decomp.decompileFunction(fn, 90, monitor)
    if res is None or not res.decompileCompleted():
        lines.append("// DECOMPILE FAILED: %s"
                     % (res.getErrorMessage() if res else "no result"))
    else:
        lines.append(res.getDecompiledFunction().getC())
    lines.append("")

d = os.path.dirname(out_path)
if d and not os.path.isdir(d):
    os.makedirs(d)
f = open(out_path, "w")
f.write("\n".join(lines))
f.close()
print("ARK: wrote %d functions to %s" % (len(targets), out_path))
