#!/usr/bin/env python3
"""Recover the ARK class graph of a module: every class it registers, and which
concrete class each abstract one delegates to.

    tools/ark_classes.py <module>.json <module>.pe <module>.iat [--json out.json]

Every Alchemy class describes itself in ONE call to the 11-argument
`igArkRegister` overload (docs/RE/ark.md). Recovering the arguments of that call
recovers the class: its name, whether it is abstract, its instance size, and the
addresses of its per-class hooks. Doing that for every call site recovers the
whole module's class table without needing a single symbol from the PE -- which
matters, because the shipped libIG*.dll files carry no PDB and only one RTTI
descriptor between them. The names come from ARK's own registration strings.

The second half is the interesting one. An abstract class records its platform
implementation by writing that class's `getClassMetaSafe` into `_Meta + 0x3c`,
and `igMetaObject::createInstance` follows that pointer in a loop. Matching each
`MOV dword ptr [<meta> + 0x3c], <fn>` against the registration table turns the
raw pointers into an ABSTRACT -> CONCRETE map, which is the substitution point a
replacement backend has to take over.

## What a negative prints

A run that finds nothing must be distinguishable from a run that never looked,
so this refuses rather than reporting zero:

  * no 11-arg `igArkRegister` slot in the .iat  -> exit non-zero, naming the
    mangled symbol it searched for;
  * a slot that exists but has no call sites    -> exit non-zero;
  * a call site whose 11 arguments cannot all be recovered -> COUNTED and listed
    individually, never silently dropped, because a partly-read argument list is
    exactly what would invent a wrong instance size.

Argument recovery walks BACKWARDS from the call collecting PUSHes, and stops at
any instruction that could disturb the stack (another CALL, a RET, a jump
target). MSVC interleaves argument pushes with unrelated scheduling, but it does
not move the stack under them, so the eleven nearest PUSHes are the eleven
arguments -- and the count is asserted, not assumed.
"""
import json
import re
import struct
import sys

# The 11-arg overload. Distinguished from the 1-arg one by its own mangling;
# matching on "igArkRegister" alone would pick up both and silently mix a
# registrar-trigger call into the class table.
ARK11 = ("?igArkRegister@Core@Gap@@YAPAV__internalFunctionList@12@_N"
         "PAPAVigMetaObject@12@P6APAV312@XZP6APAV412@XZ3PBDHP6APAXXZ"
         "P6AXXZ6PAP6AXXZ@Z")

ARG_NAMES = ["isAbstract", "metaSlot", "parentRegisterInternal",
             "parentGetClassMeta", "getClassMetaSafe", "className",
             "instanceSize", "retrieveVTablePointer", "arkRegisterInitialize",
             "arkRegisterUser", "dependentArkRegisters"]


class PE:
    def __init__(self, path):
        self.d = d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + optsz + i * 40
            vsz, va, rsz, ptr = struct.unpack_from("<IIII", d, o + 8)
            self.secs.append((va, vsz, ptr, rsz))

    def off(self, va):
        for s_va, vsz, ptr, rsz in self.secs:
            r = va - self.base - s_va
            if 0 <= r < rsz:
                return ptr + r
        return None

    def cstr(self, va, limit=192):
        o = self.off(va)
        if o is None:
            return None
        e = self.d.find(b"\0", o, o + limit)
        if e < 0:
            return None
        s = self.d[o:e]
        try:
            s = s.decode("ascii")
        except UnicodeDecodeError:
            return None
        return s if s.isprintable() else None


def iat_slot(path, mangled):
    with open(path) as f:
        for line in f:
            p = line.split(None, 2)
            if len(p) >= 3 and p[2].strip() == mangled:
                return int(p[0], 16)
    return None


PUSH_IMM = re.compile(r"^PUSH (?:dword ptr )?(?:0x([0-9a-fA-F]+)|(-?\d+))$")

# Ghidra prints an absolute memory operand two different ways -- `MOV EAX,
# [0x10188cd8]` and `MOV ESI,dword ptr [0x100cf054]` -- and both appear in the
# same function. Matching only the first form found 6 of the bindings and
# silently missed the rest, which read as "only 6 abstract classes delegate".
MEM_ABS = r"(?:dword ptr )?\[0x([0-9a-fA-F]+)\]"
LOAD_META = re.compile(r"^MOV (E[A-Z]{2}),%s$" % MEM_ABS)
STORE_ABS = re.compile(r"^MOV %s,(?:0x([0-9a-fA-F]+))$" % MEM_ABS)
STORE_OFF = re.compile(r"^MOV (?:dword ptr )?\[(E[A-Z]{2}) \+ 0x3c\],"
                       r"(?:0x([0-9a-fA-F]+))$")


def resolve_reg(ins, k, reg):
    """The constant in `reg` at instruction k, or None.

    MSVC pushes a literal 0 as `xor eax,eax; push eax`, so an argument list read
    without this reports isAbstract as a register name and every such class is
    then miscounted as concrete -- which is exactly what happened: 18 abstract
    of 100, when the true figure is larger.
    """
    for j in range(k - 1, max(-1, k - 40), -1):
        t = ins[j]["t"]
        m = re.match(r"^XOR (E[A-Z]{2}),(E[A-Z]{2})$", t)
        if m and m.group(1) == m.group(2) == reg:
            return 0
        m = re.match(r"^MOV (E[A-Z]{2}),(?:0x([0-9a-fA-F]+)|(\d+))$", t)
        if m and m.group(1) == reg:
            return int(m.group(2), 16) if m.group(2) else int(m.group(3))
        # any other write to it makes the value non-constant here
        m = re.match(r"^[A-Z]+ (E[A-Z]{2})[, ]", t + " ")
        if m and m.group(1) == reg and not t.startswith(("PUSH", "CMP", "TEST")):
            return None
    return None


def recover_args(ins, k):
    """The eleven arguments of the call at index k, arg0 first.

    Returns (args, why) -- args is None when recovery is incomplete, and `why`
    always says what stopped it. Never returns a short list as if it were whole.
    """
    args = []
    j = k - 1
    while j >= 0 and len(args) < 11:
        t = ins[j]["t"]
        m = PUSH_IMM.match(t)
        if m:
            v = int(m.group(1), 16) if m.group(1) else int(m.group(2))
            args.append(("imm", v))
        elif t.startswith("PUSH "):
            args.append(("reg", t[5:].strip()))
        elif t.startswith(("CALL", "RET", "LEAVE")) or t.startswith("ADD ESP"):
            return None, "hit %r after %d of 11 args" % (t, len(args))
        j -= 1
    if len(args) < 11:
        return None, "only %d PUSHes before the start of the function" % len(args)
    return args, None


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    jpath, pepath, iatpath = argv[0], argv[1], argv[2]
    outp = argv[argv.index("--json") + 1] if "--json" in argv else None

    slot = iat_slot(iatpath, ARK11)
    if slot is None:
        sys.exit("ark_classes: %s has NO IAT entry for the 11-argument "
                 "igArkRegister overload\n  searched for: %s\n"
                 "  This module registers no ARK classes through that call, or "
                 "the .iat is stale. Refusing rather than reporting an empty "
                 "class table." % (iatpath, ARK11))

    pe = PE(pepath)
    j = json.load(open(jpath))
    fns = j["functions"]

    call_pat = "CALL dword ptr [0x%08x]" % slot
    sites = []
    for f in fns:
        for n, i in enumerate(f.get("ins", [])):
            if i["t"] == call_pat:
                sites.append((f, n))

    if not sites:
        sys.exit("ark_classes: IAT slot 0x%08x (igArkRegister/11) exists but "
                 "NOTHING in %s calls it across %d decoded function(s). Either "
                 "the registrars were not decoded or the call is indirect; "
                 "refusing rather than reporting zero classes."
                 % (slot, jpath, len(fns)))

    classes, broken = {}, []
    for f, n in sites:
        ins = f["ins"]
        args, why = recover_args(ins, n)
        if args is None:
            broken.append((f["ep"], ins[n]["a"], why))
            continue
        kind, val = args[5]
        name = pe.cstr(val) if kind == "imm" else None
        if not name:
            broken.append((f["ep"], ins[n]["a"],
                           "argument 5 (className) is %s %r, not a readable "
                           "string" % (kind, val)))
            continue
        rec = {"name": name, "registrar": f["ep"], "site": ins[n]["a"]}
        for idx, an in enumerate(ARG_NAMES):
            k2, v2 = args[idx]
            if k2 == "imm":
                rec[an] = v2
            else:
                r = resolve_reg(ins, n, v2)
                rec[an] = r if r is not None else ("<%s>" % v2)
        classes[name] = rec

    # ---- the abstract -> concrete map, from MOV [<meta>+0x3c], <fn> ----------
    meta_of = {c["metaSlot"]: c["name"] for c in classes.values()
               if isinstance(c["metaSlot"], int)}
    by_gcms = {c["getClassMetaSafe"]: c["name"] for c in classes.values()
               if isinstance(c["getClassMetaSafe"], int)}
    # `_Meta` is loaded into a register, then +0x3c is written; also the direct
    # absolute form. Both appear.
    impl, unresolved_impl = {}, []
    nstores = 0
    for f in fns:
        held = {}          # register -> absolute address most recently loaded
        for i in f.get("ins", []):
            t = i["t"]
            m = STORE_ABS.match(t)
            if m:
                dst, val = int(m.group(1), 16), int(m.group(2), 16)
                nm = meta_of.get(dst - 0x3C)
                if nm:
                    nstores += 1
                    if val in by_gcms:
                        impl[nm] = by_gcms[val]
                    else:
                        unresolved_impl.append((nm, val))
            m = LOAD_META.match(t)
            if m:
                held[m.group(1)] = int(m.group(2), 16)
                continue
            m = STORE_OFF.match(t)
            if m and m.group(1) in held:
                nm = meta_of.get(held[m.group(1)])
                if nm:
                    nstores += 1
                    val = int(m.group(2), 16)
                    if val in by_gcms:
                        impl[nm] = by_gcms[val]
                    else:
                        unresolved_impl.append((nm, val))
            # any other write to a register invalidates what it held
            m = re.match(r"^[A-Z]+ (E[A-Z]{2}),", t)
            if m and m.group(1) in held and not LOAD_META.match(t):
                del held[m.group(1)]

    # ---- report -------------------------------------------------------------
    nabs = sum(1 for c in classes.values() if c["isAbstract"] == 1)
    nunk = sum(1 for c in classes.values()
               if not isinstance(c["isAbstract"], int))
    print("ark_classes: %s" % j.get("program", jpath))
    print("  igArkRegister/11 IAT slot 0x%08x, %d call site(s) in %d function(s)"
          % (slot, len(sites), len(fns)))
    print("  classes recovered: %d  (%d abstract, %d concrete, %d whose "
          "isAbstract stayed a register)"
          % (len(classes), nabs, len(classes) - nabs - nunk, nunk))
    # Cross-check: docs/RE/ark.md says abstract classes pass NULL for the vtable
    # retriever and concrete ones supply it. If that disagrees with isAbstract,
    # one of the two readings is wrong and neither should be trusted silently.
    dis = [c["name"] for c in classes.values()
           if isinstance(c["isAbstract"], int)
           and (c["isAbstract"] == 1) != (c["retrieveVTablePointer"] == 0)]
    print("  isAbstract vs (retrieveVTablePointer==NULL) disagreements: %d%s"
          % (len(dis), ("  " + ", ".join(dis[:8])) if dis else
             "  (the two independent readings agree)"))
    print("  _Meta+0x3c stores seen: %d" % nstores)
    print("  call sites whose arguments could NOT be fully recovered: %d"
          % len(broken))
    for ep, site, why in broken:
        print("      fn 0x%08x site 0x%08x -- %s" % (ep, site, why))
    print("  abstract -> concrete bindings via _Meta+0x3c: %d" % len(impl))
    for a, c in sorted(impl.items()):
        print("      %-34s -> %s" % (a, c))
    if unresolved_impl:
        print("  _Meta+0x3c writes whose target is NOT a class registered in "
              "THIS module (cross-module binding): %d" % len(unresolved_impl))
        for nm, val in sorted(set(unresolved_impl)):
            print("      %-34s -> 0x%08x (unknown here)" % (nm, val))

    if outp:
        with open(outp, "w") as f:
            json.dump({"classes": classes, "impl": impl,
                       "unrecovered": broken}, f, indent=1)
        print("  wrote %s" % outp)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
