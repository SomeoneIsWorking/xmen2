#@runtime Jython
# Export every identified function as JSON for the offline recompiler:
# boundaries, instruction stream (address, length, mnemonic, bytes), call
# targets, and whether each call is direct or indirect.
#
# Ghidra does the analysis that x86 makes hard -- recursive-descent boundary
# discovery and code/data separation -- and this hands the result to a plain
# Python recompiler that needs no Ghidra runtime.
#
# ENV:
#   FUNCS_OUT   output path (default scratch/recomp/<program>.json)
#   FUNCS_MAX   stop after N functions (for quick iteration; 0 = all)
import os
import json
from ghidra.program.model.symbol import RefType

out_path = os.environ.get("FUNCS_OUT", "")
limit = int(os.environ.get("FUNCS_MAX", "0"))

prog = currentProgram
listing = prog.getListing()
fm = prog.getFunctionManager()
mem = prog.getMemory()
if not out_path:
    out_path = "scratch/recomp/%s.json" % prog.getName()

# --- image layout -----------------------------------------------------------
blocks = []
for b in mem.getBlocks():
    blocks.append(dict(name=b.getName(), start=b.getStart().getOffset(),
                       size=b.getSize(), r=b.isRead(), w=b.isWrite(),
                       x=b.isExecute(), init=b.isInitialized()))

# --- symbol names for entry points -----------------------------------------
# The export table gives mangled names with full C++ signatures; keep them so
# the emitted C can be correlated back to the original API.
def qualified(fn):
    ns = fn.getParentNamespace()
    parts = [fn.getName()]
    while ns is not None and not ns.isGlobal():
        parts.append(ns.getName())
        ns = ns.getParentNamespace()
    return "::".join(reversed(parts))

funcs = []
n = 0
skipped_external = 0
for fn in fm.getFunctions(True):
    if fn.isExternal():
        skipped_external += 1
        continue
    if limit and n >= limit:
        break
    body = fn.getBody()
    ins_list = []
    for ins in listing.getInstructions(body, True):
        addr = ins.getAddress().getOffset()
        flow = None
        indirect = False
        # Resolve the flow target so the emitter can make a direct C call.
        for ref in ins.getReferencesFrom():
            rt = ref.getReferenceType()
            if rt.isCall() or rt.isJump():
                if rt.isComputed() or rt.isIndirect():
                    indirect = True
                else:
                    flow = ref.getToAddress().getOffset()
        try:
            raw = ins.getBytes()
            b = "".join("%02x" % (x & 0xFF) for x in raw)
        except Exception:
            b = ""
        rec = dict(a=addr, n=ins.getLength(), m=ins.getMnemonicString(),
                   t=str(ins), b=b)
        if flow is not None:
            rec["flow"] = flow
        if indirect:
            rec["ind"] = 1
        ins_list.append(rec)
    funcs.append(dict(
        ep=fn.getEntryPoint().getOffset(),
        name=fn.getName(),
        qname=qualified(fn),
        thunk=bool(fn.isThunk()),
        # Does this function RETURN? Ghidra knows (longjmp, exit, terminate,
        # _CxxThrowException and everything MSVC marks), and it is why the
        # analyser stops a body at such a call rather than continuing past it.
        # Without the flag the emitter cannot tell a body that ends at a
        # never-returning call -- which is COMPLETE -- from one whose
        # boundaries are wrong, and reports both as truncated.
        noret=bool(fn.hasNoReturn()),
        size=body.getNumAddresses(),
        ins=ins_list,
    ))
    n += 1

# --- imports ----------------------------------------------------------------
imports = []
for sym in prog.getSymbolTable().getExternalSymbols():
    try:
        el = sym.getObject().getExternalLocation()
        imports.append(dict(name=sym.getName(),
                            lib=el.getLibraryName() if el else ""))
    except Exception:
        imports.append(dict(name=sym.getName(), lib=""))

data = dict(program=prog.getName(),
            image_base=prog.getImageBase().getOffset(),
            blocks=blocks, functions=funcs, imports=imports)

d = os.path.dirname(out_path)
if d and not os.path.isdir(d):
    os.makedirs(d)
f = open(out_path, "w")
json.dump(data, f)
f.close()

total_ins = sum(len(x["ins"]) for x in funcs)
empty = len([1 for x in funcs if not x["ins"]])
print("FUNCS: %s -> %s" % (prog.getName(), out_path))
print("FUNCS: %d functions, %d instructions, %d imports, %d external symbols "
      "skipped" % (len(funcs), total_ins, len(imports), skipped_external))
if empty:
    # A function with no instructions means Ghidra has a body but no decoded
    # code there -- silently emitting it would look like a translated function.
    print("FUNCS: WARNING %d functions have ZERO decoded instructions and "
          "cannot be recompiled" % empty)
