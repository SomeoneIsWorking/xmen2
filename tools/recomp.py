#!/usr/bin/env python3
"""x86-32 -> C static recompiler for the X-Men Legends II PC build.

Input is the JSON emitted by tools/ghidra_scripts/ExportFuncs.py: Ghidra has
already done the part of x86 that is genuinely hard -- recursive-descent
boundary discovery and code/data separation -- so this stage is a translator,
not an analyser.

DESIGN RULE, and the reason this file is written the way it is:
**an instruction or operand form this translator does not understand must make
the affected function fail, loudly and by name.** It must never emit a comment,
a no-op, or "best effort" code. A recompiler that quietly skips instructions
produces a binary that runs and is wrong, which is far worse than one that
refuses to build -- the wrongness surfaces hours later as a divergence with no
obvious cause. So every unhandled case raises Unsupported, the function is
recorded as untranslatable with the reason, and `report` prints those reasons
ranked by how many functions each one blocks.

Usage:
  recomp.py report <funcs.json>          coverage + what is blocking it
  recomp.py emit   <funcs.json> <out.c>  translate what can be translated
"""
import json
import os
import re
import sys
from collections import Counter


class Unsupported(Exception):
    pass


# --------------------------------------------------------------- operands

REG32 = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"]
XMM = ["XMM%d" % i for i in range(8)]
REG16 = ["AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"]
REG8 = ["AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"]

LOW = {"AL": "EAX", "CL": "ECX", "DL": "EDX", "BL": "EBX"}
HIGH = {"AH": "EAX", "CH": "ECX", "DH": "EDX", "BH": "EBX"}
W16 = {"AX": "EAX", "CX": "ECX", "DX": "EDX", "BX": "EBX",
       "SP": "ESP", "BP": "EBP", "SI": "ESI", "DI": "EDI"}

PTR_SIZE = {"byte": 1, "word": 2, "dword": 4, "qword": 8, "undefined": 4,
            "undefined1": 1, "undefined2": 2, "undefined4": 4, "undefined8": 8,
            # Ghidra renders x87 memory operands with the real type, not a width
            "float": 4, "float4": 4, "double": 8, "float8": 8, "tbyte": 10,
            "longdouble": 10,
            # 128-bit SSE operands. Ghidra spells them `xmmword ptr`, and
            # `undefined16` where it has no type for the memory.
            "xmmword": 16, "oword": 16, "undefined16": 16}


def _num(tok):
    """Ghidra prints hex without 0x sometimes, and negatives as -0x..."""
    tok = tok.strip()
    neg = tok.startswith("-")
    if neg:
        tok = tok[1:]
    if tok.startswith("0x") or tok.startswith("0X"):
        v = int(tok, 16)
    elif re.fullmatch(r"[0-9a-fA-F]+", tok) and re.search(r"[a-fA-F]", tok):
        v = int(tok, 16)
    elif re.fullmatch(r"[0-9]+", tok):
        v = int(tok, 10)
    else:
        raise Unsupported("immediate %r" % tok)
    return -v if neg else v


class Operand(object):
    """kind: reg32 / reg16 / reg8lo / reg8hi / imm / mem"""

    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)

    # --- as an rvalue expression of the operand's own width
    def read(self):
        if self.kind == "reg32":
            return "C->%s" % self.reg.lower()
        if self.kind == "reg16":
            return "(uint16_t)(C->%s)" % W16[self.reg].lower()
        if self.kind == "reg8lo":
            return "(uint8_t)(C->%s)" % LOW[self.reg].lower()
        if self.kind == "reg8hi":
            return "(uint8_t)(C->%s >> 8)" % HIGH[self.reg].lower()
        if self.kind == "imm":
            r = img_rel(self.val & 0xFFFFFFFF)
            return r if r else "0x%xU" % (self.val & 0xFFFFFFFF)
        if self.kind == "mem":
            return "RD%d(%s)" % (self.size * 8, self.addr())
        if self.kind == "seg":
            return "SEGRD32(%s, %s)" % (self.seg, self.addr())
        raise Unsupported("read %s" % self.kind)

    def write(self, expr):
        if self.kind == "reg32":
            return "C->%s = %s;" % (self.reg.lower(), expr)
        if self.kind == "reg16":
            r = W16[self.reg].lower()
            return "C->%s = (C->%s & 0xFFFF0000U) | ((%s) & 0xFFFFU);" % (r, r, expr)
        if self.kind == "reg8lo":
            r = LOW[self.reg].lower()
            return "C->%s = (C->%s & 0xFFFFFF00U) | ((%s) & 0xFFU);" % (r, r, expr)
        if self.kind == "reg8hi":
            r = HIGH[self.reg].lower()
            return "C->%s = (C->%s & 0xFFFF00FFU) | (((%s) & 0xFFU) << 8);" % (r, r, expr)
        if self.kind == "mem":
            return "WR%d(%s, %s);" % (self.size * 8, self.addr(), expr)
        if self.kind == "seg":
            return "SEGWR32(%s, %s, %s);" % (self.seg, self.addr(), expr)
        raise Unsupported("write %s" % self.kind)

    def addr(self):
        return self.addr_expr

    @property
    def width(self):
        if self.kind in ("reg32", "imm"):
            return 4
        if self.kind == "reg16":
            return 2
        if self.kind in ("reg8lo", "reg8hi"):
            return 1
        if self.kind == "seg":
            return 4
        return self.size


def parse_operand(tok):
    tok = tok.strip()
    if not tok:
        raise Unsupported("empty operand")
    up = tok.upper()
    if up in REG32:
        return Operand("reg32", reg=up)
    if up in XMM:
        return Operand("xmm", reg=up, idx=int(up[3:]))
    if up in REG16:
        return Operand("reg16", reg=up)
    if up in LOW:
        return Operand("reg8lo", reg=up)
    if up in HIGH:
        return Operand("reg8hi", reg=up)

    m = re.match(r"^(byte|word|dword|qword|xmmword|oword|float\d*|double"
                 r"|tbyte|longdouble|undefined\d*)\s+ptr\s+(.*)$", tok, re.I)
    if m and re.match(r"^(FS|GS):", m.group(2).strip(), re.I):
        return parse_operand(m.group(2).strip())
    if m:
        size = PTR_SIZE.get(m.group(1).lower())
        if size is None:
            raise Unsupported("ptr size %r" % m.group(1))
        return parse_mem(m.group(2), size)
    if tok.startswith("[") and tok.endswith("]"):
        o = parse_mem(tok, 4)
        o.inferred = True          # width not stated; caller must reconcile it
        return o

    # FS-relative access is MSVC's exception-frame prologue (MOV EAX,FS:[0],
    # MOV FS:[0],ESP). Emitted as a read/write through the runtime's FS base
    # rather than modelled per-field.
    #
    # WHICH host that base belongs to differs, and the earlier note here named
    # only one of them: under Wine the code really does run as a 32-bit PE and
    # FS is the genuine TIB, but x2native is a 64-bit ELF host with no TIB at
    # all, and there src/recomp/x86rt.h resolves this to a flat block the
    # runtime owns (x2native.c's TIB_BASE). Both work for the prologue.
    #
    # NEITHER provides exception DELIVERY. The chain is well-formed enough to
    # be pushed and popped; nothing walks it if the guest actually throws.
    # x2native.c states the same gap at the other end.  GS is deliberately
    # refused: the shipped corpus has exactly two GS instructions, both the
    # byte sequence 65 00 00 inside functions already known to run through
    # embedded data.  Treating those bytes as executable ADDs made the hosted
    # MinGW build demand GS intrinsics that do not exist, hiding the boundary
    # defect behind a compiler error.
    m2 = re.match(r"^(FS|GS):(.*)$", tok, re.I)
    if m2:
        seg = m2.group(1).upper()
        if seg == "GS":
            raise Unsupported("GS segment override (the shipped occurrences "
                              "are embedded data decoded as code)")
        inner = m2.group(2).strip()
        mm = re.match(r"^\[(.*)\]$", inner)
        off = mm.group(1) if mm else inner
        mo = parse_mem("[%s]" % off, 4)
        return Operand("seg", seg=seg, size=4, addr_expr=mo.addr_expr)
    if re.match(r"^(CS|DS|ES|SS):", up):
        raise Unsupported("segment override %s" % up.split(":")[0])

    try:
        return Operand("imm", val=_num(tok))
    except Unsupported:
        pass
    # a bare symbol: a data address or a code label Ghidra named
    if re.match(r"^[A-Za-z_][\w:.@?$<>]*$", tok):
        raise Unsupported("symbolic operand %r" % tok)
    raise Unsupported("operand %r" % tok)


def parse_mem(tok, size):
    tok = tok.strip()
    m = re.match(r"^\[(.*)\]$", tok)
    if not m:
        # absolute address without brackets
        v = _num(tok) & 0xFFFFFFFF
        return Operand("mem", size=size, inferred=False,
                       addr_expr=img_rel(v) or "0x%xU" % v)
    inner = m.group(1).strip()
    if re.match(r"^(CS|DS|ES|SS):", inner, re.I):
        raise Unsupported("segment-relative memory")

    # base [+ index*scale] [+/- disp]
    parts = re.split(r"\s*([+\-])\s*", inner)
    expr = []
    sign = "+"
    for p in parts:
        if p in ("+", "-"):
            sign = p
            continue
        p = p.strip()
        if not p:
            continue
        term = None
        ms = re.match(r"^([A-Za-z]+)\s*\*\s*(0x[0-9a-fA-F]+|\d+)$", p)
        if ms:
            r = ms.group(1).upper()
            if r not in REG32:
                raise Unsupported("index register %r" % r)
            term = "(C->%s * %d)" % (r.lower(), _num(ms.group(2)))
        elif p.upper() in REG32:
            term = "C->%s" % p.lower()
        else:
            v = _num(p) & 0xFFFFFFFF
            term = img_rel(v) or "0x%xU" % v
        expr.append((sign, term))
        sign = "+"
    if not expr:
        raise Unsupported("empty memory expression %r" % tok)
    out = expr[0][1] if expr[0][0] == "+" else "(0U - %s)" % expr[0][1]
    for s, t in expr[1:]:
        out = "%s %s %s" % (out, s, t)
    return Operand("mem", size=size, addr_expr="(uint32_t)(%s)" % out)


def reconcile(a, b):
    """An untyped memory operand takes its width from the other operand.
    Ghidra prints `MOV AL,[0x10021b7a]` with no `byte ptr`, and defaulting such
    an operand to 4 bytes would silently read 3 bytes too many."""
    for x, y in ((a, b), (b, a)):
        if x.kind == "mem" and getattr(x, "inferred", False) and y.kind != "mem":
            if y.kind == "imm":
                continue           # immediate carries no width
            x.size = y.width
            x.inferred = False
    return a, b


def split_operands(s):
    """Split on commas that are not inside brackets."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out]


# --------------------------------------------------------------- emitting

# Condition -> C expression over the lazy-flag state.
CC = {
    "Z": "FLAG_Z(C)", "NZ": "!FLAG_Z(C)",
    "C": "FLAG_C(C)", "NC": "!FLAG_C(C)",
    "S": "FLAG_S(C)", "NS": "!FLAG_S(C)",
    "O": "FLAG_O(C)", "NO": "!FLAG_O(C)",
    "P": "FLAG_P(C)", "NP": "!FLAG_P(C)",
    "A": "(!FLAG_C(C) && !FLAG_Z(C))", "BE": "(FLAG_C(C) || FLAG_Z(C))",
    "B": "FLAG_C(C)", "AE": "!FLAG_C(C)",
    "L": "(FLAG_S(C) != FLAG_O(C))", "GE": "(FLAG_S(C) == FLAG_O(C))",
    "LE": "(FLAG_Z(C) || (FLAG_S(C) != FLAG_O(C)))",
    "G": "(!FLAG_Z(C) && (FLAG_S(C) == FLAG_O(C)))",
}

ARITH = {"ADD": "+", "SUB": "-", "AND": "&", "OR": "|", "XOR": "^"}
FLAGKIND = {"ADD": "FK_ADD", "SUB": "FK_SUB", "AND": "FK_LOGIC",
            "OR": "FK_LOGIC", "XOR": "FK_LOGIC", "CMP": "FK_SUB",
            "TEST": "FK_LOGIC", "INC": "FK_INC", "DEC": "FK_DEC"}


IAT = {}          # absolute VA of an import slot -> (module, symbol)
IMG = [0, 0]      # [preferred base, end] of the module being recompiled
IMG_REBASED = [0]  # immediates rewritten as image-relative; reported by emit
# (truncated fn ep, address it falls into) for every body that ends without a
# terminator and runs into code that is not a known function. Written out by
# emit as <module>.trunc -- the input ghidra_export.sh --merge wants.
FALL_DEADEND = []
# A branch target that is INSIDE a function but is not its entry: target ->
# owning entry point. MSVC shares one epilogue between paths, so `JMP` lands in
# the middle of another function -- 28 targets from 38 sites in XMen2.exe, the
# worked example being 0x0066cf3c inside FUN_0066ced2 (issue #29). Such a
# target has no function name, so it used to become x86_call_unknown and stop
# the run; now the owning body can be ENTERED AT THAT LABEL.
INTERIOR = {}
KNOWN_EPS = set()  # entry points Ghidra identified; a call to anything else is
                   # a target inside the region it could not resolve into
                   # functions, and must not be emitted as a direct C call


def interior_entries(functions):
    """{interior target -> owning entry point} for direct JMPs across bodies.

    Jumps only -- unconditional and conditional alike, since a Jcc is just a
    predicated jump: no return address, no stack change, same mechanism. Both
    occur: 13 targets from 16 JMP sites and 17 from 22 Jcc sites, 28 distinct
    addresses in all.

    Only ACROSS functions. A jump within a function is already a goto, and a
    jump to a function's entry is already a tail call -- routing either through
    an entry check would be slower and no more correct.

    CALLs are deliberately excluded. There are none in this image (measured: 0
    sites), and a call needs its return address pushed before the entry, so
    including it would ship a path no run has ever exercised. One that appears
    keeps reporting itself by name.
    """
    eps = set(f["ep"] for f in functions)
    owner = {}
    for f in functions:
        for i in f["ins"]:
            owner[i["a"]] = f["ep"]
    out = {}
    for f in functions:
        body = set(i["a"] for i in f["ins"])
        for i in f["ins"]:
            t = i.get("flow")
            if t is None or not i["m"].upper().startswith("J"):
                continue
            if t in body or t in eps or t not in owner:
                continue
            out[t] = owner[t]
    return out


def img_rel(val):
    """An absolute address inside the module's own image is emitted relative to
    the module's RUNTIME base, not its preferred base.

    This is not cosmetic. The original DLL only loads at 0x10000000 when nothing
    else claims that address; inside the game it is relocated (observed at
    0x001C0000), and every hardcoded 0x100xxxxx reference then reads unrelated
    memory -- silently, because the address is still mapped. The difftest passed
    only because in that small process the DLL did get its preferred base."""
    if IMG[0] <= val < IMG[1]:
        IMG_REBASED[0] += 1
        return "(G_IMGBASE + 0x%xU)" % (val - IMG[0])
    return None


def ret_push(ret):
    """The return address a CALL pushes, as the guest will SEE it.

    It goes through img_rel like every other address into this module. It did
    not, and pushed the LINKED address instead -- which is self-consistent as
    long as nothing looks at it, because the matching RET pops the same value
    and compares it against the same constant. Anything that USES it breaks:
    Gap::Core::igMetaObject::arkRegister pushes a function pointer, calls
    igArkRegister, and the ARK machinery ends up transferring to the stacked
    return address. With libIGCore relocated to 0x24000000 that address was
    0x100177da, which resolves into whichever module occupies 0x10000000 --
    libIGUtils -- and the dispatcher reported a missing body in the wrong
    module entirely (issue #15 follow-on). The guest stack has to hold real
    addresses.
    """
    return "C->esp -= 4; WR32(C->esp, %s);" % (img_rel(ret) or "0x%xU" % ret)


def icall(t, ret):
    """An indirect CALL: read the target with the ESP the instruction was
    REACHED with, then push the return address.

    On real x86 the memory operand is computed before the push. Emitting the
    push first shifts ESP by four, so every esp-relative target -- `call dword
    ptr [esp+8]`, the shape MSVC gives a callback invoked through a stack
    argument -- is read four bytes low and dispatches to whatever is there. In
    Gap::Core::igArkRegister that is the caller's return address, so ARK
    registration jumped into the middle of arkRegister instead of calling the
    registration function it was handed.

    This is the SAME defect that 8a70f81 fixed in the Xbox lifter
    (vendor/xboxrecomp) on 2026-08-05. It was never fixed here: the two
    translators are separate codebases and nothing checked the second one when
    the first was corrected. Worth remembering as a class, not an incident.
    """
    return ["{ uint32_t _icall = %s;" % t.read(),
            ret_push(ret),
            "DISPATCH(C, _icall); }"]


def iat_symbol(addr_expr):
    """If a memory operand is a bare absolute address that is an IAT slot,
    return its (module, symbol); else None."""
    m = re.fullmatch(r"\(uint32_t\)\(\(G_IMGBASE \+ (0x[0-9a-f]+)U\)\)", addr_expr)
    if not m:
        m = re.fullmatch(r"\(G_IMGBASE \+ (0x[0-9a-f]+)U\)", addr_expr)
    if m:
        return IAT.get(IMG[0] + int(m.group(1), 16))
    m = re.fullmatch(r"\(uint32_t\)\((0x[0-9a-f]+)U\)", addr_expr) or \
        re.fullmatch(r"(0x[0-9a-f]+)U", addr_expr)
    return IAT.get(int(m.group(1), 16)) if m else None


def c_ident(mod, sym):
    return "imp_%s_%s" % (re.sub(r"\W", "_", mod.split(".")[0]),
                          re.sub(r"\W", "_", sym))


# Import thunks, by entry point.
#
# MSVC routes a call to an import through a one-instruction thunk -- `JMP
# dword ptr [IAT]` -- so the call site reads `CALL 0x0067281a`, not `CALL
# [IAT]`. That matters for exactly one import: _setjmp3 has to be emitted
# INLINE in the body that calls it (see x86rt.h), and the body calls the
# thunk. Without this map the special case never fired, and the emitted code
# called a stub whose frame cannot be jumped back into.
SETJMP_THUNKS = set()


def find_setjmp_thunks(functions):
    """Entry points of thunks that forward to _setjmp3."""
    SETJMP_THUNKS.clear()
    for f in functions:
        ins = f.get("ins") or []
        if len(ins) != 1:
            continue
        i = ins[0]
        if i["m"].upper() != "JMP" or not i.get("ind"):
            continue
        m = re.search(r"\[(0x[0-9a-f]+)\]", i["t"])
        if not m:
            continue
        sym = IAT.get(int(m.group(1), 16))
        if sym and sym[1] == "_setjmp3":
            SETJMP_THUNKS.add(f["ep"])
    return SETJMP_THUNKS


RECORD_RANGES = []          # [(lo, hi)] -- see x86_record in x86rt.h


def in_record_range(a):
    for lo, hi in RECORD_RANGES:
        if lo <= a <= hi:
            return True
    return False


def emit_instruction(ins, ctx):
    """Return a list of C lines, or raise Unsupported."""
    m = ins["m"].upper()
    text = ins["t"]
    # operand text is everything after the mnemonic in Ghidra's rendering
    rest = text[len(ins["m"]):].strip() if text.upper().startswith(m) else ""
    ops = split_operands(rest) if rest else []
    L = []
    A = "/* %08x %s */" % (ins["a"], text)

    def O(i):
        return parse_operand(ops[i])

    if m == "NOP":
        return ["%s" % A]

    if m == "INT3":
        # MSVC emits INT3 as an unreachable trap after a call to a noreturn
        # function, and as inter-block padding. It is not padding BETWEEN
        # functions here -- these occurrences are inside real bodies, and
        # refusing to translate them blocked 545 of libIGSg's 6118 functions
        # (9%) over an instruction that, in a correct run, never executes.
        # Translated as a stop that names its address: if it ever DOES execute,
        # control reached code the compiler proved unreachable, and that is
        # worth knowing loudly rather than skipping the whole function for.
        # MAPPED: x86_int3 calls where(), which resolves the address against
        # the module table, and a linked address names whichever module happens
        # to occupy 0x10000000 (C101).
        return ["%s" % A, "  x86_int3(%s);"
                % (img_rel(ins["a"]) or "0x%08xU" % ins["a"])]

    if m in ("WAIT", "FWAIT"):
        # FWAIT waits for pending unmasked x87 exceptions. This runtime has
        # none to wait for: x87_fault stops AT the offending instruction rather
        # than flagging the status word and deferring. So the wait has nothing
        # to check, which makes it a genuine no-op here rather than a skipped
        # instruction -- and if lazy x87 exceptions are ever modelled, this is
        # the one place that has to change.
        return ["%s" % A]

    if m == "CLD":
        # Clears the direction flag, which this runtime does not model: string
        # operations are emitted in the ascending form unconditionally. CLD is
        # therefore asserting what is already assumed. STD would NOT be, and is
        # not translated -- so a module that sets DF still refuses rather than
        # quietly running backwards.
        return ["%s" % A]

    if m == "MOV":
        if len(ops) != 2:
            raise Unsupported("MOV with %d operands" % len(ops))
        d, s = reconcile(O(0), O(1))
        if d.width != s.width and s.kind != "imm":
            raise Unsupported("MOV width mismatch %d<-%d" % (d.width, s.width))
        return [A, d.write(s.read())]

    if m in ("MOVZX", "MOVSX"):
        d, s = O(0), O(1)
        src = s.read()
        if m == "MOVSX":
            cast = "int8_t" if s.width == 1 else "int16_t"
            src = "(uint32_t)(int32_t)(%s)(%s)" % (cast, src)
        else:
            src = "(uint32_t)(%s)" % src
        return [A, d.write(src)]

    if m == "LEA":
        d, s = O(0), O(1)
        if s.kind != "mem":
            raise Unsupported("LEA from non-memory")
        return [A, d.write(s.addr())]

    #
    # PUSH and POP read their operand BEFORE moving ESP, and write it AFTER.
    #
    # Intel is explicit and it is not a detail: `PUSH r/m32` computes the
    # effective address of a memory operand using the ORIGINAL ESP, and `POP
    # r/m32` computes a destination address using ESP as it is AFTER the
    # increment. Emitting `C->esp -= 4; WR32(C->esp, RD32(C->esp + 8));` reads
    # [esp-4+8] = [esp+4] -- one dword off, and only for operands based on ESP.
    #
    # This was live. XMen2.exe 0x0065e314 is a four-instruction allocator whose
    # whole body is `PUSH dword ptr [ESP+8]; CALL malloc`, and it passed the
    # WRONG stack slot: a caller's pointer instead of the size 0x50 its caller
    # had pushed. malloc got 0x700ff678, failed, and the game took its own
    # out-of-memory path and died calling a handler it had never installed --
    # three issues (#24, #25, #26) away from the instruction that caused it.
    #
    if m == "PUSH":
        s = O(0)
        if s.width != 4:
            raise Unsupported("PUSH width %d" % s.width)
        return [A, "{ uint32_t _v = %s;" % s.read(),
                "  C->esp -= 4; WR32(C->esp, _v); }"]

    if m == "POP":
        d = O(0)
        if d.width != 4:
            raise Unsupported("POP width %d" % d.width)
        return [A, "{ uint32_t _v = RD32(C->esp);",
                "  C->esp += 4;",
                "  " + d.write("_v"), "}"]

    if m in ARITH:
        d, s = reconcile(O(0), O(1))
        w = d.width
        L.append(A)
        L.append("{ uint32_t _a = %s, _b = %s, _r;" % (d.read(), s.read()))
        L.append("  _r = _a %s _b;" % ARITH[m])
        L.append("  SETFLAGS(C, %s, _a, _b, _r, %d);" % (FLAGKIND[m], w))
        L.append("  " + d.write("_r"))
        L.append("}")
        return L

    if m in ("CMP", "TEST"):
        d, s = reconcile(O(0), O(1))
        w = d.width
        op = "-" if m == "CMP" else "&"
        return [A,
                "{ uint32_t _a = %s, _b = %s, _r = _a %s _b;" % (d.read(), s.read(), op),
                "  SETFLAGS(C, %s, _a, _b, _r, %d); }" % (FLAGKIND[m], w)]

    if m in ("INC", "DEC"):
        d = O(0)
        w = d.width
        delta = "+ 1" if m == "INC" else "- 1"
        return [A,
                "{ uint32_t _a = %s, _r = _a %s;" % (d.read(), delta),
                "  SETFLAGS_NC(C, %s, _a, 1, _r, %d);" % (FLAGKIND[m], w),
                "  " + d.write("_r") + " }"]

    # ---- x87. Memory operand sizes: Ghidra prints `float ptr` / `dword ptr`
    # for m32 and `qword ptr` for m64; integer forms (FILD/FIDIV/FIMUL) take
    # the value as a signed integer, not as a float.
    if m in ("FLD", "FILD", "FSTP", "FST", "FADD", "FSUB", "FSUBR", "FMUL",
             "FDIV", "FDIVR", "FIDIV", "FIDIVR", "FIMUL", "FIADD", "FISUB",
             "FCOM", "FCOMP", "FCOMPP", "FUCOM", "FUCOMP", "FUCOMPP",
             "FNSTSW", "FCHS", "FABS", "FSQRT", "FISTP", "FIST", "FLDZ",
             "FLD1", "FLDPI", "FLDLG2", "FLDLN2", "FLDL2E", "FLDL2T",
             "FXCH", "FADDP", "FSUBP", "FSUBRP", "FMULP", "FDIVP", "FDIVRP",
             "FRNDINT", "FPREM", "FSCALE", "FXTRACT", "FNSTCW", "FLDCW",
             "FSTCW", "FSTSW",
             "FYL2X", "FYL2XP1", "FPATAN", "F2XM1", "FSIN", "FCOS",
             "FSINCOS", "FTST", "FPTAN",
             "FFREE", "FINCSTP", "FDECSTP", "FNCLEX", "FNINIT"):
        def fsrc(o, integer):
            if o.kind == "mem":
                if integer:
                    # FILD/FIADD/... take m16, m32 or m64, and the width is
                    # the operand's -- not a default. Emitting RDI32 for every
                    # one of them read the low dword of a 64-bit value and
                    # sign-extended it, which is issue #35: the engine's 64-bit
                    # nanosecond timer wrapped negative after 2.147 seconds.
                    if o.size not in (2, 4, 8):
                        raise Unsupported("%s from a %d-byte integer operand"
                                          % (m, o.size))
                    return "RDI%d(%s)" % (o.size * 8, o.addr())
                if o.size not in (4, 8):
                    raise Unsupported("%s from a %d-byte float operand"
                                      % (m, o.size))
                return ("RDF64(%s)" % o.addr()) if o.size == 8 else "RDF32(%s)" % o.addr()
            raise Unsupported("%s from %s" % (m, o.kind))

        # constants
        CONST = {"FLDZ": "0.0L", "FLD1": "1.0L",
                 "FLDPI": "3.14159265358979323846L",
                 "FLDLG2": "0.30102999566398119521L",   # log10(2)
                 "FLDLN2": "0.69314718055994530942L",   # ln(2)
                 "FLDL2E": "1.44269504088896340736L",   # log2(e)
                 "FLDL2T": "3.32192809488736234787L"}   # log2(10)
        if m in CONST:
            return [A, "x87_push(C, %s);" % CONST[m]]
        if m == "FXCH":
            i2 = re.fullmatch(r"ST(\d)", ops[0].strip().upper()).group(1) if ops else "1"
            return [A,
                    "{ long double _t = X87_ST(C, 0);",
                    "  X87_ST(C, 0) = X87_ST(C, %s); X87_ST(C, %s) = _t; }" % (i2, i2)]
        if m in ("FADDP", "FSUBP", "FSUBRP", "FMULP", "FDIVP", "FDIVRP"):
            o2 = {"FADDP": "+", "FSUBP": "-", "FSUBRP": "-", "FMULP": "*",
                  "FDIVP": "/", "FDIVRP": "/"}[m]
            dst = re.fullmatch(r"ST(\d)", ops[0].strip().upper()).group(1) if ops else "1"
            if m in ("FSUBRP", "FDIVRP"):
                expr = "X87_ST(C, 0) %s X87_ST(C, %s)" % (o2, dst)
            else:
                expr = "X87_ST(C, %s) %s X87_ST(C, 0)" % (dst, o2)
            return [A, "X87_ST(C, %s) = %s;" % (dst, expr), "(void)x87_pop(C);"]
        if m in ("FCOMPP", "FUCOMPP"):
            return [A, "x87_cmp(C, X87_ST(C, 0), X87_ST(C, 1));",
                    "(void)x87_pop(C); (void)x87_pop(C);"]
        if m in ("FYL2X", "FYL2XP1"):
            arg = "X87_ST(C, 0)" if m == "FYL2X" else "(X87_ST(C, 0) + 1.0L)"
            return [A,
                    "X87_ST(C, 1) = X87_ST(C, 1) * __builtin_log2l(%s);" % arg,
                    "(void)x87_pop(C);"]
        if m == "FPATAN":
            return [A,
                    "X87_ST(C, 1) = __builtin_atan2l(X87_ST(C, 1), X87_ST(C, 0));",
                    "(void)x87_pop(C);"]
        if m == "F2XM1":
            return [A, "X87_ST(C, 0) = __builtin_exp2l(X87_ST(C, 0)) - 1.0L;"]
        if m == "FPTAN":
            # Replaces ST(0) with its tangent and then PUSHES 1.0, so the
            # stack GROWS. Computing only the tangent would leave every later
            # ST(n) off by one, silently and only on this path. (The 1.0 is
            # there so FDIVP can turn the tangent into a cotangent.)
            return [A, "X87_ST(C, 0) = __builtin_tanl(X87_ST(C, 0));",
                    "x87_push(C, 1.0L);"]
        if m in ("FSIN", "FCOS"):
            fn_ = "__builtin_sinl" if m == "FSIN" else "__builtin_cosl"
            return [A, "X87_ST(C, 0) = %s(X87_ST(C, 0));" % fn_]
        if m == "FSINCOS":
            return [A, "{ long double _s = __builtin_sinl(X87_ST(C, 0));",
                    "  long double _c = __builtin_cosl(X87_ST(C, 0));",
                    "  X87_ST(C, 0) = _s; x87_push(C, _c); }"]
        if m == "FTST":
            return [A, "x87_cmp(C, X87_ST(C, 0), 0.0L);"]
        if m == "FSQRT":
            return [A, "X87_ST(C, 0) = __builtin_sqrtl(X87_ST(C, 0));"]
        if m == "FRNDINT":
            return [A, "X87_ST(C, 0) = __builtin_rintl(X87_ST(C, 0));"]
        if m in ("FINCSTP", "FDECSTP", "FFREE", "FNCLEX"):
            # Stack-pointer housekeeping with no value semantics we model.
            return [A, "/* %s: no modelled effect */" % m]
        if m == "FNINIT":
            # FINIT restores the POWER-ON control word too, not just the
            # stack and status. Leaving fcw alone would let a guest that
            # unmasked exceptions keep them unmasked across an FINIT that
            # says otherwise.
            return [A, "C->top = 0; C->depth = 0; C->fsw = 0;",
                    "C->fcw = X87_CW_INIT;"]
        # FSTCW/FSTSW are FNSTCW/FNSTSW preceded by an implicit FWAIT: the
        # WAIT checks for a PENDING unmasked x87 exception before storing.
        # This runtime raises nothing lazily -- x87_fault stops at the
        # instruction that caused it -- so there is never a pending exception
        # to wait for, and the waiting and non-waiting forms are the same
        # instruction here. Not "close enough": the state the wait inspects
        # does not exist in this model.
        if m in ("FNSTCW", "FSTCW", "FLDCW"):
            o = O(0)
            if o.kind != "mem":
                raise Unsupported("%s with %s operand" % (m, o.kind))
            if m == "FLDCW":
                return [A, "C->fcw = RD16(%s);" % o.addr()]
            return [A, "WR16(%s, (uint16_t)C->fcw);" % o.addr()]
        if m in ("FNSTSW", "FSTSW"):
            # The AX form is the common one (FCOM then test AH). The memory
            # form stores the same 16 bits.
            if ops and O(0).kind == "mem":
                return [A, "WR16(%s, (uint16_t)C->fsw);" % O(0).addr()]
            return [A, "C->eax = (C->eax & 0xFFFF0000U) | (C->fsw & 0xFFFFU);"]
        if m == "FLDZ":
            return [A, "x87_push(C, 0.0L);"]
        if m == "FLD1":
            return [A, "x87_push(C, 1.0L);"]
        if m == "FCHS":
            return [A, "X87_ST(C, 0) = -X87_ST(C, 0);"]
        if m == "FABS":
            return [A, "X87_ST(C, 0) = (X87_ST(C, 0) < 0) ? -X87_ST(C, 0) : X87_ST(C, 0);"]
        if not ops:
            # FCOM/FCOMP with no operand compare ST(0) against ST(1)
            if m in ("FCOM", "FCOMP", "FUCOM", "FUCOMP"):
                L2 = [A, "x87_cmp(C, X87_ST(C, 0), X87_ST(C, 1));"]
                if m in ("FCOMP", "FUCOMP"):
                    L2.append("(void)x87_pop(C);")
                return L2
            if m in ("FADD", "FSUB", "FSUBR", "FMUL", "FDIV", "FDIVR"):
                o2 = {"FADD": "+", "FSUB": "-", "FSUBR": "-", "FMUL": "*",
                      "FDIV": "/", "FDIVR": "/"}[m]
                if m in ("FSUBR", "FDIVR"):
                    return [A, "X87_ST(C, 1) = X87_ST(C, 0) %s X87_ST(C, 1);" % o2,
                            "(void)x87_pop(C);"]
                return [A, "X87_ST(C, 1) = X87_ST(C, 1) %s X87_ST(C, 0);" % o2,
                        "(void)x87_pop(C);"]
            raise Unsupported("%s with no operand" % m)

        st = re.fullmatch(r"ST(\d)", ops[0].strip().upper())
        if m in ("FLD", "FILD"):
            if st:
                return [A, "x87_push(C, X87_ST(C, %s));" % st.group(1)]
            return [A, "x87_push(C, %s);" % fsrc(O(0), m == "FILD")]
        if m in ("FSTP", "FST", "FISTP", "FIST"):
            if st:
                # The REGISTER forms, and both halves of this were wrong.
                #
                # FST ST(i) does not pop at all; emitting a pop for it drains
                # the modelled stack one slot per execution until it
                # underflows. FSTP ST(i) does pop, but it stores FIRST: writing
                # the popped value into X87_ST(i) afterwards indexes the
                # POST-pop stack, so `FSTP ST(1)` landed in what had been
                # ST(2). 2981 FSTP ST(i) and 2 FST ST(i) in this image.
                if m == "FST":
                    return [A, "X87_ST(C, %s) = X87_ST(C, 0);" % st.group(1)]
                return [A,
                        "{ long double _v = X87_ST(C, 0);",
                        "  X87_ST(C, %s) = _v; (void)x87_pop(C); }" % st.group(1)]
            o = O(0)
            if o.kind != "mem":
                raise Unsupported("%s to %s" % (m, o.kind))
            pop = m in ("FSTP", "FISTP")
            val = "x87_pop(C)" if pop else "X87_ST(C, 0)"
            if m in ("FISTP", "FIST"):
                # Same width rule as FILD, and the same defect if it is
                # ignored: `FISTP qword ptr` writing 32 bits leaves the high
                # half of a 64-bit result whatever it was.
                if o.size not in (2, 4, 8):
                    raise Unsupported("%s to a %d-byte integer operand"
                                      % (m, o.size))
                if o.size == 8:
                    return [A, "WR64(%s, (uint64_t)x87_to_i64(C, %s));"
                            % (o.addr(), val)]
                w = "WR16" if o.size == 2 else "WR32"
                return [A, "%s(%s, (uint32_t)x87_to_int(C, %s));"
                        % (w, o.addr(), val)]
            return [A, "%s(%s, %s);"
                    % ("WRF64" if o.size == 8 else "WRF32", o.addr(), val)]
        if m in ("FCOM", "FCOMP", "FUCOM", "FUCOMP"):
            src = "X87_ST(C, %s)" % st.group(1) if st else fsrc(O(0), False)
            L2 = [A, "x87_cmp(C, X87_ST(C, 0), %s);" % src]
            if m in ("FCOMP", "FUCOMP"):
                L2.append("(void)x87_pop(C);")
            return L2
        # arithmetic: ST(0) op= src, or the two-register form
        opc = {"FADD": "+", "FIADD": "+", "FSUB": "-", "FISUB": "-",
               "FSUBR": "-", "FMUL": "*", "FIMUL": "*", "FDIV": "/",
               "FIDIV": "/", "FIDIVR": "/", "FDIVR": "/"}[m]
        rev = m in ("FSUBR", "FDIVR", "FIDIVR")
        if st and len(ops) >= 2:
            st2 = re.fullmatch(r"ST(\d)", ops[1].strip().upper())
            if not st2:
                raise Unsupported("%s %s,%s" % (m, ops[0], ops[1]))
            a_, b_ = "X87_ST(C, %s)" % st.group(1), "X87_ST(C, %s)" % st2.group(1)
            if rev:
                return [A, "%s = %s %s %s;" % (a_, b_, opc, a_)]
            return [A, "%s = %s %s %s;" % (a_, a_, opc, b_)]
        src = "X87_ST(C, %s)" % st.group(1) if st else fsrc(O(0), m[:2] == "FI")
        if rev:
            return [A, "X87_ST(C, 0) = %s %s X87_ST(C, 0);" % (src, opc)]
        return [A, "X87_ST(C, 0) = X87_ST(C, 0) %s %s;" % (opc, src)]

    if m in ("MUL", "IMUL", "DIV", "IDIV"):
        # One-operand forms use EDX:EAX implicitly. IMUL also has 2- and
        # 3-operand forms that do NOT touch EDX -- they are handled separately
        # because treating them as the implicit form would clobber EDX.
        if m == "IMUL" and len(ops) >= 2:
            d, s2 = reconcile(O(0), O(1))
            if len(ops) == 3:
                a_, b_ = O(1).read(), O(2).read()
            else:
                a_, b_ = d.read(), s2.read()
            return [A,
                    "{ int32_t _r = (int32_t)(%s) * (int32_t)(%s);" % (a_, b_),
                    "  SETFLAGS(C, FK_LOGIC, 0U, 0U, (uint32_t)_r, 4);",
                    "  " + d.write("(uint32_t)_r") + " }"]
        src = O(0)
        if src.width == 1:
            # AX = AL * src8
            cast = "uint32_t" if m == "MUL" else "int32_t"
            conv = "(uint8_t)" if m == "MUL" else "(int8_t)"
            return [A,
                    "{ %s _p = (%s)%s(C->eax) * (%s)%s(%s);"
                    % (cast, cast, conv, cast, conv, src.read()),
                    "  C->eax = (C->eax & 0xFFFF0000U) | ((uint32_t)_p & 0xFFFFU);",
                    "  SETFLAGS(C, FK_LOGIC, 0U, 0U, (uint32_t)_p, 4); }"]
        if src.width != 4:
            raise Unsupported("%s with %d-byte operand" % (m, src.width))
        if m in ("MUL", "IMUL"):
            cast = "uint64_t" if m == "MUL" else "int64_t"
            conv = "(uint32_t)" if m == "MUL" else "(int32_t)"
            return [A,
                    "{ %s _p = (%s)%s(C->eax) * (%s)%s(%s);"
                    % (cast, cast, conv, cast, conv, src.read()),
                    "  C->eax = (uint32_t)_p; C->edx = (uint32_t)(_p >> 32);",
                    "  SETFLAGS(C, FK_LOGIC, 0U, 0U, C->edx, 4); }"]
        # DIV / IDIV: divide-by-zero is #DE on hardware; aborting is the honest
        # analogue -- returning a made-up quotient would corrupt silently.
        if m == "DIV":
            return [A,
                    "{ uint64_t _n = ((uint64_t)C->edx << 32) | C->eax;",
                    "  uint32_t _d = %s;" % src.read(),
                    "  if (!_d) x87_fault(\"DIV by zero\");",
                    "  C->eax = (uint32_t)(_n / _d); C->edx = (uint32_t)(_n % _d); }"]
        return [A,
                "{ int64_t _n = (int64_t)(((uint64_t)C->edx << 32) | C->eax);",
                "  int32_t _d = (int32_t)%s;" % src.read(),
                "  if (!_d) x87_fault(\"IDIV by zero\");",
                "  C->eax = (uint32_t)(int32_t)(_n / _d);",
                "  C->edx = (uint32_t)(int32_t)(_n % _d); }"]

    #
    # SSE packed logic, on a real 128-bit register file.
    #
    # These arrived because Gap::Core::igGetCPUCaps PROBES for SSE by executing
    # `ORPS XMM0,XMM0` under SEH -- an identity by value, there only to fault if
    # the OS will not allow SSE. It would have been easy to special-case that one
    # instruction as a no-op, and wrong: the no-op would be right by accident and
    # the next ORPS with different operands would be silently incorrect. A real
    # register file makes the probe a no-op BECAUSE x|x == x, which is the same
    # answer for the right reason.
    #
    if m in ("ORPD", "ANDPD", "XORPD"):
        # The double-precision forms are bit-identical to the single ones, but
        # nothing in these modules uses them; translating them on that
        # reasoning alone would ship untested code.
        raise Unsupported("%s (packed-double logic; no module uses it)" % m)

    #
    # SSE single precision.
    #
    # `alignedMatrixMultiplySSE` in libIGMath is on the skinning path, so the
    # first level with an animated character reaches it. The whole set below is
    # what the shipped modules actually contain -- counted, not guessed:
    # MOVAPS/MULPS/SHUFPS/MOVSS/ADDPS lead at over 600 uses each. Anything
    # outside it still refuses by name.
    #
    SSE_BIN = {
        "ADDPS": "sse_addps", "SUBPS": "sse_subps", "MULPS": "sse_mulps",
        "DIVPS": "sse_divps", "MINPS": "sse_minps", "MAXPS": "sse_maxps",
        "ANDPS": "sse_andps", "ANDNPS": "sse_andnps", "ORPS": "sse_orps",
        "XORPS": "sse_xorps",
        "ADDSS": "sse_addss", "SUBSS": "sse_subss", "MULSS": "sse_mulss",
        "DIVSS": "sse_divss", "MINSS": "sse_minss", "MAXSS": "sse_maxss",
        "UNPCKLPS": "sse_unpcklps", "UNPCKHPS": "sse_unpckhps",
        "MOVHLPS": "sse_movhlps", "MOVLHPS": "sse_movlhps",
        "SQRTPS": "sse_sqrtps", "RCPPS": "sse_rcpps", "RSQRTPS": "sse_rsqrtps",
        "SQRTSS": "sse_sqrtss", "RCPSS": "sse_rcpss", "RSQRTSS": "sse_rsqrtss",
    }
    # Ghidra spells the compare predicate into the mnemonic; the imm8 encoding
    # is what the model takes.
    SSE_CMP = {"EQ": 0, "LT": 1, "LE": 2, "UNORD": 3,
               "NEQ": 4, "NLT": 5, "NLE": 6, "ORD": 7}

    def xmm_reg(o):
        return o.idx if o.kind == "xmm" else None

    def xmm_src(o, tok):
        """A `const uint64_t *` to the source's 16 bytes.

        A memory source is pointed AT rather than copied: guest memory is
        identity-mapped, which is the same assumption RD64 makes two hundred
        lines up. MOVAPS's 16-byte alignment requirement is not enforced --
        hardware would fault, and this does not; the guest is compiler output
        that satisfies it, and a check on every load would cost more than it
        can find."""
        if o.kind == "xmm":
            return "C->xmm[%d]" % o.idx
        if o.kind == "mem":
            return "(const uint64_t *)(uintptr_t)(%s)" % o.addr()
        raise Unsupported("%s with source operand %r" % (m, tok.strip()))

    if m in SSE_BIN:
        d, s2 = O(0), O(1)
        if xmm_reg(d) is None:
            raise Unsupported("%s with a non-XMM destination" % m)
        # MOVHLPS/MOVLHPS exist only in the register-to-register encoding.
        if m in ("MOVHLPS", "MOVLHPS") and s2.kind != "xmm":
            raise Unsupported("%s with a memory source, which has no encoding"
                              % m)
        return [A, "%s(C->xmm[%d], %s);"
                % (SSE_BIN[m], d.idx, xmm_src(s2, ops[1]))]

    if m.startswith("CMP") and (m.endswith("PS") or m.endswith("SS")):
        pred = SSE_CMP.get(m[3:-2])
        if pred is None:
            raise Unsupported("%s (unknown SSE compare predicate %r)"
                              % (m, m[3:-2]))
        d, s2 = O(0), O(1)
        if xmm_reg(d) is None:
            raise Unsupported("%s with a non-XMM destination" % m)
        fn = "sse_cmpps" if m.endswith("PS") else "sse_cmpss"
        return [A, "%s(C->xmm[%d], %s, %d);"
                % (fn, d.idx, xmm_src(s2, ops[1]), pred)]

    if m == "SHUFPS":
        if len(ops) != 3:
            raise Unsupported("SHUFPS with %d operand(s)" % len(ops))
        d, s2, imm = O(0), O(1), O(2)
        if xmm_reg(d) is None or imm.kind != "imm":
            raise Unsupported("SHUFPS %s,%s,%s"
                              % (ops[0].strip(), ops[1].strip(),
                                 ops[2].strip()))
        return [A, "sse_shufps(C->xmm[%d], %s, 0x%02xU);"
                % (d.idx, xmm_src(s2, ops[1]), imm.val & 0xFF)]

    if m in ("MOVAPS", "MOVUPS", "MOVNTPS"):
        # MOVNTPS differs only in cache behaviour, which is not observable
        # here; MOVAPS and MOVUPS differ only in the alignment fault.
        d, s2 = O(0), O(1)
        if d.kind == "xmm" and s2.kind == "xmm":
            return [A, "C->xmm[%d][0] = C->xmm[%d][0];" % (d.idx, s2.idx),
                    "C->xmm[%d][1] = C->xmm[%d][1];" % (d.idx, s2.idx)]
        if d.kind == "xmm" and s2.kind == "mem":
            return [A, "{ uint32_t _a128 = %s;" % s2.addr(),
                    "  C->xmm[%d][0] = RD64(_a128);" % d.idx,
                    "  C->xmm[%d][1] = RD64(_a128 + 8U); }" % d.idx]
        if d.kind == "mem" and s2.kind == "xmm":
            return [A, "{ uint32_t _a128 = %s;" % d.addr(),
                    "  WR64(_a128, C->xmm[%d][0]);" % s2.idx,
                    "  WR64(_a128 + 8U, C->xmm[%d][1]); }" % s2.idx]
        raise Unsupported("%s %s,%s" % (m, ops[0].strip(), ops[1].strip()))

    if m == "MOVSS":
        d, s2 = O(0), O(1)
        if d.kind == "xmm" and s2.kind == "xmm":
            return [A, "sse_movss_rr(C->xmm[%d], C->xmm[%d]);"
                    % (d.idx, s2.idx)]
        if d.kind == "xmm" and s2.kind == "mem":
            # The memory form ZEROES lanes 1..3; the register form does not.
            return [A, "sse_movss_load(C->xmm[%d], RD32(%s));"
                    % (d.idx, s2.addr())]
        if d.kind == "mem" and s2.kind == "xmm":
            return [A, "WR32(%s, (uint32_t)C->xmm[%d][0]);"
                    % (d.addr(), s2.idx)]
        raise Unsupported("MOVSS %s,%s" % (ops[0].strip(), ops[1].strip()))

    if m in ("MOVLPS", "MOVHPS"):
        half = 0 if m == "MOVLPS" else 1
        d, s2 = O(0), O(1)
        if d.kind == "xmm" and s2.kind == "mem":
            return [A, "C->xmm[%d][%d] = RD64(%s);"
                    % (d.idx, half, s2.addr())]
        if d.kind == "mem" and s2.kind == "xmm":
            return [A, "WR64(%s, C->xmm[%d][%d]);"
                    % (d.addr(), s2.idx, half)]
        raise Unsupported("%s %s,%s -- the register-to-register form is "
                          "MOVHLPS/MOVLHPS and is spelled that way"
                          % (m, ops[0].strip(), ops[1].strip()))

    if m == "MOVMSKPS":
        d, s2 = O(0), O(1)
        if s2.kind != "xmm":
            raise Unsupported("MOVMSKPS from %r" % ops[1].strip())
        return [A, d.write("sse_movmskps(C->xmm[%d])" % s2.idx)]

    if m in ("COMISS", "UCOMISS"):
        # Identical here: they differ only in which NaNs raise an exception,
        # and the exceptions are masked.
        d, s2 = O(0), O(1)
        if d.kind != "xmm":
            raise Unsupported("%s with a non-XMM first operand" % m)
        return [A, "sse_comiss(C, C->xmm[%d], %s);"
                % (d.idx, xmm_src(s2, ops[1]))]

    if m in ("PREFETCHNTA", "PREFETCHT0", "PREFETCHT1", "PREFETCHT2",
             "SFENCE", "LFENCE", "MFENCE"):
        # Genuinely architecturally invisible: cache hints and, on a
        # single-guest-thread-at-a-time runtime with one global lock, fences
        # over a memory model no weaker than x86's.
        return [A, "/* %s: a hint with no architectural effect here */" % m]

    if m in ("ADC", "SBB"):
        d, s2 = reconcile(O(0), O(1))
        w = d.width
        op = "+" if m == "ADC" else "-"
        # Flags computed EXACTLY, not squeezed into the lazy (a, b, r) triple:
        # the carry IN does not fit it, and the approximation that did ship got
        # the borrow wrong -- see x86_flags_sbb in x86rt.h.
        helper = "x86_flags_adc" if m == "ADC" else "x86_flags_sbb"
        return [A,
                "{ uint32_t _a = %s, _b = %s, _c = FLAG_C(C) ? 1U : 0U, _r;"
                % (d.read(), s2.read()),
                "  _r = _a %s _b %s _c;" % (op, op),
                "  SETFLAGS(C, FK_EXPLICIT, %s(_a, _b, _c, _r, %d), 0U, _r, %d);"
                % (helper, w, w),
                "  " + d.write("_r") + " }"]

    # REP string ops. DF is assumed clear -- justified by measurement, not
    # convention: this module contains no STD or CLD at all, so DF is whatever
    # the ABI guarantees on entry (clear). If a module with STD appears, this
    # must become an error rather than keep assuming.
    if m in ("STOSD.REP", "STOSB.REP", "STOSW.REP", "MOVSD.REP", "MOVSB.REP",
             "MOVSW.REP", "STOSD", "STOSB", "STOSW", "MOVSD", "MOVSB", "MOVSW"):
        rep = ".REP" in m
        unit = 1 if m[4] == "B" else (2 if m[4] == "W" else 4)
        bits = unit * 8
        if m.startswith("STOS"):
            src = "(uint8_t)C->eax" if unit == 1 else "C->eax"
            body = "WR%d(C->edi, %s); C->edi += %d;" % (bits, src, unit)
        else:
            body = ("WR%d(C->edi, RD%d(C->esi)); C->esi += %d; C->edi += %d;"
                    % (bits, bits, unit, unit))
        if rep:
            return [A, "while (C->ecx) { %s C->ecx--; }" % body]
        return [A, body]        # single-step form, no REP prefix

    if m in ("CMPSD.REPE", "CMPSB.REPE"):
        unit = 1 if "B." in m else 4
        bits = unit * 8
        return [A,
                "{ uint32_t _a = 0, _b = 0;",
                "  while (C->ecx) {",
                "    _a = RD%d(C->esi); _b = RD%d(C->edi);" % (bits, bits),
                "    C->esi += %d; C->edi += %d; C->ecx--;" % (unit, unit),
                "    if (_a != _b) break;",
                "  }",
                "  SETFLAGS(C, FK_SUB, _a, _b, (uint32_t)(_a - _b), %d); }" % unit]

    if m in ("SCASB.REPNE", "SCASD.REPNE"):
        unit = 1 if "B." in m else 4
        bits = unit * 8
        acc = "(uint8_t)C->eax" if unit == 1 else "C->eax"
        return [A,
                "{ uint32_t _a = %s, _b = 0;" % acc,
                "  while (C->ecx) {",
                "    _b = RD%d(C->edi); C->edi += %d; C->ecx--;" % (bits, unit),
                "    if (_a == _b) break;",
                "  }",
                "  SETFLAGS(C, FK_SUB, _a, _b, (uint32_t)(_a - _b), %d); }" % unit]

    if m == "NEG":
        d = O(0)
        w = d.width
        return [A,
                "{ uint32_t _a = %s, _r = (uint32_t)(0U - _a);" % d.read(),
                "  SETFLAGS(C, FK_SUB, 0U, _a, _r, %d);" % w,
                "  " + d.write("_r") + " }"]

    if m == "CPUID":
        # Report the REAL CPU. Faking a minimal feature set would push the CRT
        # onto its generic paths and hide the SSE/MMX routines rather than
        # translate them; if it selects a routine we have not translated, the
        # hybrid fallback runs the original for it, which is correct.
        return [A,
                "{ unsigned _r[4];",
                "  __cpuid((int *)_r, (int)C->eax);",
                "  C->eax = _r[0]; C->ebx = _r[1];",
                "  C->ecx = _r[2]; C->edx = _r[3]; }"]
    if m == "RDTSC":
        return [A,
                "{ unsigned long long _t = __rdtsc();",
                "  C->eax = (uint32_t)_t; C->edx = (uint32_t)(_t >> 32); }"]

    if m in ("PUSHAD", "PUSHA"):
        # The ESP that is pushed is the value BEFORE any of the pushes, which
        # is the whole reason a temporary is needed: writing C->esp into the
        # fifth slot after four decrements stores a value 16 bytes low, and
        # POPAD discards that slot so nothing would ever complain.
        return [A,
                "{ uint32_t _sp = C->esp;",
                "  C->esp -= 4; WR32(C->esp, C->eax);",
                "  C->esp -= 4; WR32(C->esp, C->ecx);",
                "  C->esp -= 4; WR32(C->esp, C->edx);",
                "  C->esp -= 4; WR32(C->esp, C->ebx);",
                "  C->esp -= 4; WR32(C->esp, _sp);",
                "  C->esp -= 4; WR32(C->esp, C->ebp);",
                "  C->esp -= 4; WR32(C->esp, C->esi);",
                "  C->esp -= 4; WR32(C->esp, C->edi); }"]
    if m in ("POPAD", "POPA"):
        # ESP is SKIPPED, not restored -- the processor discards that slot, and
        # restoring it would undo the pops that follow it.
        return [A,
                "C->edi = RD32(C->esp); C->esp += 4;",
                "C->esi = RD32(C->esp); C->esp += 4;",
                "C->ebp = RD32(C->esp); C->esp += 4;",
                "C->esp += 4;                       /* the saved ESP */",
                "C->ebx = RD32(C->esp); C->esp += 4;",
                "C->edx = RD32(C->esp); C->esp += 4;",
                "C->ecx = RD32(C->esp); C->esp += 4;",
                "C->eax = RD32(C->esp); C->esp += 4;"]

    if m in ("PUSHFD", "PUSHF"):
        return [A, "C->esp -= 4; WR32(C->esp, x86_eflags(C));"]
    if m in ("POPFD", "POPF"):
        return [A,
                "{ uint32_t _f = RD32(C->esp); C->esp += 4;",
                "  SETFLAGS(C, FK_EXPLICIT, _f, 0U, 0U, 4); }"]
    if m in ("CLC", "STC", "CMC"):
        op = {"CLC": "& ~1U", "STC": "| 1U", "CMC": "^ 1U"}[m]
        return [A,
                "{ uint32_t _f = x86_eflags(C) %s;" % op,
                "  SETFLAGS(C, FK_EXPLICIT, _f, 0U, 0U, 4); }"]
    if m == "LAHF":
        return [A, "C->eax = (C->eax & 0xFFFF00FFU) | "
                   "((x86_eflags(C) & 0xFFU) << 8);"]
    if m == "SAHF":
        return [A,
                "{ uint32_t _f = (x86_eflags(C) & 0xFFFFFF00U) | "
                "((C->eax >> 8) & 0xFFU);",
                "  SETFLAGS(C, FK_EXPLICIT, _f, 0U, 0U, 4); }"]

    if m == "NOT":
        d = O(0)
        return [A, d.write("~(%s)" % d.read())]

    if m in ("SHL", "SHR", "SAR"):
        d, s = reconcile(O(0), O(1))
        w = d.width
        if m == "SHL":
            body = "_a << _c"
        elif m == "SHR":
            body = "_a >> _c"
        else:
            body = "(uint32_t)(((int32_t)(_a << (32 - %d*8))) >> (_c + (32 - %d*8)))" % (w, w)
        return [A,
                "{ uint32_t _a = %s, _c = (%s) & 31, _r;" % (d.read(), s.read()),
                "  _r = %s;" % body,
                "  if (_c) SETFLAGS_SHIFT(C, _a, _c, _r, %d);" % w,
                "  " + d.write("_r") + " }"]

    if m in ("ROL", "ROR", "RCL", "RCR"):
        # The count is an imm8 or CL even when the destination is 32-bit, and
        # the D0/D1 encodings have no count operand at all -- it is 1.
        d = O(0)
        cnt = O(1).read() if len(ops) > 1 else "1U"
        return [A,
                "{ uint32_t _f = x86_eflags(C), _r;",
                "  _r = x86_rotate(%s, %s, %d, X86_%s, &_f);"
                % (d.read(), cnt, d.width, m),
                "  SETFLAGS(C, FK_EXPLICIT, _f, 0U, 0U, 4);",
                "  " + d.write("_r") + " }"]

    if m in ("SHLD", "SHRD"):
        d, s2 = O(0), O(1)
        cnt = O(2).read() if len(ops) > 2 else "(C->ecx & 31)"
        if m == "SHLD":
            body = ("(_a << _c) | (_c ? (_b >> (32 - _c)) : 0U)")
        else:
            body = ("(_a >> _c) | (_c ? (_b << (32 - _c)) : 0U)")
        return [A,
                "{ uint32_t _a = %s, _b = %s, _c = (%s) & 31, _r;"
                % (d.read(), s2.read(), cnt),
                "  _r = %s;" % body,
                "  if (_c) SETFLAGS_SHIFT(C, _a, _c, _r, 4);",
                "  " + d.write("_r") + " }"]

    # MMX lane arithmetic. The semantics live in x86rt.h as per-lane C; this
    # is only the operand plumbing, because that is where the differences
    # between these instructions are NOT -- every one of them is
    # `dst = f(dst, src)` over MM registers, with the source allowed to be a
    # 64-bit memory operand and, for the shifts, an immediate count.
    #
    # A mnemonic that is NOT in this table still raises Unsupported and is
    # reported by name, so adding half the family cannot silently leave the
    # other half looking translated.
    MMX_BIN = {
        "PADDB": "mmx_paddb", "PADDW": "mmx_paddw", "PADDD": "mmx_paddd",
        "PSUBB": "mmx_psubb", "PSUBW": "mmx_psubw", "PSUBD": "mmx_psubd",
        "PADDUSB": "mmx_paddusb", "PADDUSW": "mmx_paddusw",
        "PSUBUSB": "mmx_psubusb", "PSUBUSW": "mmx_psubusw",
        "PADDSB": "mmx_paddsb", "PADDSW": "mmx_paddsw",
        "PSUBSB": "mmx_psubsb", "PSUBSW": "mmx_psubsw",
        "PMULLW": "mmx_pmullw", "PMULHW": "mmx_pmulhw",
        "PMULHUW": "mmx_pmulhuw", "PMADDWD": "mmx_pmaddwd",
        "PAVGB": "mmx_pavgb", "PAVGW": "mmx_pavgw",
        "PCMPEQB": "mmx_pcmpeqb", "PCMPEQW": "mmx_pcmpeqw",
        "PCMPEQD": "mmx_pcmpeqd", "PCMPGTB": "mmx_pcmpgtb",
        "PCMPGTW": "mmx_pcmpgtw", "PCMPGTD": "mmx_pcmpgtd",
        "PMINUB": "mmx_pminub", "PMAXUB": "mmx_pmaxub",
        "PMINSW": "mmx_pminsw", "PMAXSW": "mmx_pmaxsw",
        "PACKUSWB": "mmx_packuswb", "PACKSSWB": "mmx_packsswb",
        "PACKSSDW": "mmx_packssdw",
        "PUNPCKLBW": "mmx_punpcklbw", "PUNPCKHBW": "mmx_punpckhbw",
        "PUNPCKLWD": "mmx_punpcklwd", "PUNPCKHWD": "mmx_punpckhwd",
        "PUNPCKLDQ": "mmx_punpckldq", "PUNPCKHDQ": "mmx_punpckhdq",
    }
    MMX_LOGIC = {"PAND": "&", "POR": "|", "PXOR": "^"}
    MMX_SHIFT = {
        "PSLLW": "mmx_psllw", "PSLLD": "mmx_pslld", "PSLLQ": "mmx_psllq",
        "PSRLW": "mmx_psrlw", "PSRLD": "mmx_psrld", "PSRLQ": "mmx_psrlq",
        "PSRAW": "mmx_psraw", "PSRAD": "mmx_psrad",
    }

    def mmx_reg(tok):
        mm = re.fullmatch(r"MM(\d)", tok.strip().upper())
        return int(mm.group(1)) if mm else None

    def mmx_src(tok):
        """The 64-bit value of an MMX source: a register or a memory operand.
        An XMM operand is NOT accepted -- these are the 64-bit forms, and the
        128-bit ones would need the other register file and a second lane
        pair."""
        r = mmx_reg(tok)
        if r is not None:
            return "C->mm[%d]" % r
        o = parse_operand(tok)
        if o.kind == "mem":
            return "RD64(%s)" % o.addr()
        raise Unsupported("MMX source operand %r" % tok.strip())

    if m in MMX_BIN or m in MMX_LOGIC or m in MMX_SHIFT:
        if len(ops) != 2:
            raise Unsupported("%s with %d operand(s)" % (m, len(ops)))
        d = mmx_reg(ops[0])
        if d is None:
            raise Unsupported("%s into %r, which is not an MMX register"
                              % (m, ops[0].strip()))
        if m in MMX_LOGIC:
            return [A, "C->mm[%d] %s= %s;" % (d, MMX_LOGIC[m], mmx_src(ops[1]))]
        if m in MMX_SHIFT:
            # The count may be an immediate; everything else is a 64-bit value.
            o = parse_operand(ops[1])
            cnt = ("%dULL" % o.val) if o.kind == "imm" else mmx_src(ops[1])
            return [A, "C->mm[%d] = %s(C->mm[%d], %s);"
                    % (d, MMX_SHIFT[m], d, cnt)]
        return [A, "C->mm[%d] = %s(C->mm[%d], %s);"
                % (d, MMX_BIN[m], d, mmx_src(ops[1]))]

    # PEXTRW r32, MMn, imm8 -- one 16-bit lane into a general register, zero
    # extended. It is SSE1 rather than MMX, but it operates on the MMX file.
    if m == "PEXTRW":
        if len(ops) != 3:
            raise Unsupported("PEXTRW with %d operand(s)" % len(ops))
        src = mmx_reg(ops[1])
        sel = parse_operand(ops[2])
        if src is None or sel.kind != "imm":
            raise Unsupported("PEXTRW %s,%s,%s"
                              % (ops[0].strip(), ops[1].strip(), ops[2].strip()))
        d = parse_operand(ops[0])
        return [A, d.write("(uint32_t)((C->mm[%d] >> %d) & 0xFFFFU)"
                           % (src, (sel.val & 3) * 16))]

    # MMX: registers modelled separately from the x87 stack (see x86rt.h).
    if m in ("MOVQ", "MOVD", "EMMS", "FEMMS"):
        if m in ("EMMS", "FEMMS"):
            return [A, "/* %s: MMX/x87 aliasing not modelled; see x86rt.h */" % m]
        def mmx(tok):
            mm = re.fullmatch(r"MM(\d)", tok.strip().upper())
            return mm.group(1) if mm else None
        a_, b_ = ops[0].strip(), ops[1].strip()
        # The SSE2 spelling of the same mnemonics moves 32 or 64 bits between
        # an XMM register and memory or a general register. Loading into XMM
        # ZEROES the rest of the register; storing writes only the low part.
        # Parsed only when one side really is an XMM register: an `MM0` token
        # is not a parseable operand, so parsing both unconditionally turned
        # every MMX MOVD/MOVQ into "symbolic operand" -- 72 of them.
        is_xmm = re.compile(r"^XMM\d$", re.I)
        if is_xmm.match(a_) or is_xmm.match(b_):
            xa, xb = parse_operand(a_), parse_operand(b_)
            lo = "C->xmm[%d][0]"
            if xa.kind == "xmm" and xb.kind != "xmm":
                v = ("RD64(%s)" % xb.addr()) if m == "MOVQ" and \
                    xb.kind == "mem" else \
                    ("(uint64_t)RD32(%s)" % xb.addr()) if xb.kind == "mem" \
                    else "(uint64_t)(%s)" % xb.read()
                return [A, (lo % xa.idx) + " = %s;" % v,
                        "C->xmm[%d][1] = 0;" % xa.idx]
            if xb.kind == "xmm" and xa.kind != "xmm":
                if xa.kind == "mem":
                    return [A, ("WR64(%s, C->xmm[%d][0]);"
                                % (xa.addr(), xb.idx)) if m == "MOVQ"
                            else ("WR32(%s, (uint32_t)C->xmm[%d][0]);"
                                  % (xa.addr(), xb.idx))]
                return [A, xa.write("(uint32_t)C->xmm[%d][0]" % xb.idx)]
            # XMM to XMM: MOVQ keeps the low quadword and zeroes the high one.
            if m == "MOVQ":
                return [A, "C->xmm[%d][0] = C->xmm[%d][0];" % (xa.idx, xb.idx),
                        "C->xmm[%d][1] = 0;" % xa.idx]
            raise Unsupported("MOVD between two XMM registers, which has no "
                              "encoding")
        ma, mb = mmx(a_), mmx(b_)
        if ma and mb:
            return [A, "C->mm[%s] = C->mm[%s];" % (ma, mb)]
        if ma:
            o = parse_operand(b_)
            if o.kind == "mem":
                sz = 8 if m == "MOVQ" else 4
                return [A, "C->mm[%s] = %s;" % (ma,
                        "RD64(%s)" % o.addr() if sz == 8 else "(uint64_t)RD32(%s)" % o.addr())]
            return [A, "C->mm[%s] = (uint64_t)(%s);" % (ma, o.read())]
        if mb:
            o = parse_operand(a_)
            if o.kind == "mem":
                sz = 8 if m == "MOVQ" else 4
                return [A, ("WR64(%s, C->mm[%s]);" % (o.addr(), mb)) if sz == 8
                        else ("WR32(%s, (uint32_t)C->mm[%s]);" % (o.addr(), mb))]
            return [A, o.write("(uint32_t)C->mm[%s]" % mb)]
        raise Unsupported("%s %s,%s" % (m, a_, b_))

    if m == "XCHG":
        a, b = reconcile(O(0), O(1))
        return [A, "{ uint32_t _t = %s;" % a.read(),
                "  " + a.write(b.read()),
                "  " + b.write("_t") + " }"]

    if m == "CDQ":
        return [A, "C->edx = (uint32_t)((int32_t)C->eax >> 31);"]
    if m == "CWDE":
        return [A, "C->eax = (uint32_t)(int32_t)(int16_t)C->eax;"]

    if m == "LEAVE":
        return [A, "C->esp = C->ebp; C->ebp = RD32(C->esp); C->esp += 4;"]

    if m == "RET":
        # A function may deliberately alter its own return address --
        # __SEH_prolog does exactly that, and it is the second function the exe
        # executes. Returning to the C caller regardless would silently resume
        # in the wrong place, so compare the popped value against the address
        # this function was entered with and tail-dispatch when they differ.
        n = 0
        if ops:
            o = parse_operand(ops[0])
            if o.kind != "imm":
                raise Unsupported("RET with non-immediate")
            n = o.val
        return [A,
                "{ uint32_t _rt = RD32(C->esp); C->esp += 4 + %d;" % n,
                "  if (_rt != _retaddr) { x86_return_to(C, _rt, _x86_fn_ep, _retaddr); }",
                "  X86_EXIT_FN(_x86_fn_ep);",
                "  return; }"]

    if m.startswith("SET"):
        cc = m[3:]
        if cc not in CC:
            raise Unsupported("SETcc %s" % cc)
        d = O(0)
        return [A, d.write("(%s) ? 1U : 0U" % CC[cc])]

    if m in ("CALL", "JMP") and ops and not (m == "JMP" and "flow" in ins
                                             and not ins.get("ind")):
        # A call/jump through a bare absolute address is an IMPORT, not an
        # unresolvable indirect: the PE import table names the callee.
        try:
            t = parse_operand(ops[0])
        except Unsupported:
            t = None
        if t is not None and t.kind == "mem":
            sym = iat_symbol(t.addr())
            if sym:
                ret = ins["a"] + ins["n"]
                call = "%s(C);" % c_ident(*sym)
                if sym[1] == "_setjmp3":
                    # NOT a call to a stub, and it cannot be one.
                    #
                    # A host longjmp resumes into a frame that must still be
                    # alive. An import stub's frame is dead the moment it
                    # returns, so a setjmp taken inside one could never be
                    # jumped back to -- which is why this is emitted HERE, in
                    # the body that contains the guest's own setjmp call site.
                    # x86rt.h documents the pair; crt.c implements it.
                    body = ["{ int _sj = setjmp(*x86_setjmp_buf(C));",
                            "  x86_setjmp_done(C, _sj); }"]
                    if m == "CALL":
                        return [A, ret_push(ret)] + body
                    # The IMPORT THUNK itself -- one instruction, JMP [IAT].
                    # Its frame is as dead as the stub's, so a setjmp taken
                    # here could not be resumed either; it falls through to the
                    # stub, which records the buffer as unresumable and says so
                    # by name if a longjmp ever arrives at it. Real call sites
                    # do not come through here: setjmp_thunks() below routes
                    # them to the inline form in their own body.
                if m == "CALL":
                    return [A, ret_push(ret), call]
                return [A, call, "return;"]     # tail-jump thunk
            # otherwise it is real indirect dispatch (vtable etc.)
            ret = ins["a"] + ins["n"]
            if m == "CALL":
                return [A] + icall(t, ret)
            return [A, "{ _injmp = %s; goto L_injmp; }" % t.read()] \
                   if ctx.get("_has_injmp") else \
                   [A, "DISPATCH(C, %s); return;" % t.read()]
        if t is not None and t.kind in ("reg32",):
            ret = ins["a"] + ins["n"]
            if m == "CALL":
                return [A] + icall(t, ret)
            return [A, "{ _injmp = %s; goto L_injmp; }" % t.read()] \
                   if ctx.get("_has_injmp") else \
                   [A, "DISPATCH(C, %s); return;" % t.read()]

    # A JMP whose target lies outside this function is a TAIL CALL, not a
    # branch -- MSVC emits these for one-line wrappers. Treating it as a goto
    # produces `label used but not defined`, which is how it was caught.
    if m == "JMP":
        if ins.get("ind") or "flow" not in ins:
            raise Unsupported("indirect JMP")
        if ins["flow"] not in ctx["_addrs"]:
            if ins["flow"] in INTERIOR:
                # Into the MIDDLE of another body: a shared MSVC epilogue. The
                # owner is entered at the label rather than at its entry, using
                # the same offset switch its computed jumps already use. The
                # address is MAPPED (img_rel), because that switch subtracts
                # G_IMGBASE and a linked address would match no case.
                return [A, "{ C->enter_at = %s; %s(C); return; }"
                        % (img_rel(ins["flow"]) or "0x%08xU" % ins["flow"],
                           fname(INTERIOR[ins["flow"]]))]
            if KNOWN_EPS and ins["flow"] not in KNOWN_EPS:
                # MAPPED, not linked: every libIG*.dll is linked for
                # 0x10000000, so a raw linked address makes the runtime name
                # whichever module happens to occupy that range. It reported a
                # missing function in libCriMovie that was really libIGGui's,
                # and the discovery loop then seeded and split the wrong module
                # -- successfully, and to no effect. Same class as C093.
                return [A, "x86_call_unknown(C, %s); return;"
                        % (img_rel(ins["flow"]) or "0x%08xU" % ins["flow"])]
            return [A, "%s(C); return;" % fname(ins["flow"])]
        return [A, "goto L_%08x;" % ins["flow"]]

    if m in ("LOOP", "LOOPE", "LOOPZ", "LOOPNE", "LOOPNZ"):
        # ECX is decremented BEFORE the test and the FLAGS are untouched --
        # both differ from `DEC ECX; JNZ`, which is the shape it is easy to
        # emit instead. LOOPE/LOOPNE additionally test ZF, and they read the
        # flag left by an earlier instruction, not one this produces.
        if "flow" not in ins:
            raise Unsupported("%s with no resolved target" % m)
        if ins["flow"] not in ctx["_addrs"]:
            raise Unsupported("%s out of its own body (target 0x%08x)"
                              % (m, ins["flow"]))
        extra = {"LOOP": "", "LOOPE": " && FLAG_Z(C)", "LOOPZ": " && FLAG_Z(C)",
                 "LOOPNE": " && !FLAG_Z(C)", "LOOPNZ": " && !FLAG_Z(C)"}[m]
        return [A, "if (--C->ecx != 0U%s) goto L_%08x;" % (extra, ins["flow"])]

    if m.startswith("J") and m[1:] in CC:
        if "flow" not in ins:
            raise Unsupported("conditional jump with no resolved target")
        if ins["flow"] not in ctx["_addrs"]:
            if ins["flow"] in INTERIOR:
                # Into another body at a label -- see the JMP case above. A Jcc
                # is a predicated jump, so nothing else about it differs.
                return [A, "if (%s) { C->enter_at = %s; %s(C); return; }"
                        % (CC[m[1:]],
                           img_rel(ins["flow"]) or "0x%08xU" % ins["flow"],
                           fname(INTERIOR[ins["flow"]]))]
            if KNOWN_EPS and ins["flow"] not in KNOWN_EPS:
                return [A, "if (%s) { x86_call_unknown(C, %s); return; }"
                        % (CC[m[1:]],
                           img_rel(ins["flow"]) or "0x%08xU" % ins["flow"])]
            return [A, "if (%s) { %s(C); return; }" % (CC[m[1:]], fname(ins["flow"]))]
        return [A, "if (%s) goto L_%08x;" % (CC[m[1:]], ins["flow"])]

    if m == "CALL":
        if ins.get("ind") or "flow" not in ins:
            raise Unsupported("indirect CALL")
        ret = ins["a"] + ins["n"]
        if ins["flow"] in SETJMP_THUNKS:
            # Before the known-entry-point test, not after: this is decided by
            # what the target IS, and routing it through x86_call_unknown first
            # would emit a call to a stub whose frame cannot be resumed into.
            # See the _setjmp3 case in the indirect branch above.
            return [A, ret_push(ret),
                    "{ int _sj = setjmp(*x86_setjmp_buf(C));",
                    "  x86_setjmp_done(C, _sj); }"]
        if KNOWN_EPS and ins["flow"] not in KNOWN_EPS:
            # Ghidra did not identify a function at this target. Emitting
            # fn_<addr> would simply fail to link; routing it through the
            # runtime keeps the module buildable and makes reaching it a
            # located, named failure instead of a silent one.
            return [A,
                    ret_push(ret),
                    "x86_call_unknown(C, %s);"
                    % (img_rel(ins["flow"]) or "0x%08xU" % ins["flow"])]
        return [A,
                ret_push(ret),
                "%s(C);" % fname(ins["flow"])]

    raise Unsupported("mnemonic %s" % m)


def translate(fn):
    """-> (list of C lines, None) or (None, reason)."""
    body = []
    fn["_addrs"] = set(i["a"] for i in fn["ins"])
    targets = set()
    for ins in fn["ins"]:
        # LOOP is a branch too. Collecting only `J*` left its target without a
        # label, and the failure was a COMPILE error rather than a wrong
        # translation -- which is the good kind, but only because C requires
        # labels to exist.
        if "flow" in ins and (ins["m"].upper().startswith("J")
                              or ins["m"].upper().startswith("LOOP")):
            if ins["flow"] in fn["_addrs"]:
                targets.add(ins["flow"])
    #
    # A computed JMP -- a switch -- lands on a case label INSIDE this function,
    # which is not a function entry, so dispatching it globally can only fail:
    # `igGetCPUCaps` is a 59-case switch and the run stopped on "no recompiled
    # body at 0x1006790e", an address 0x9e into the function it was already in.
    #
    # So a function containing one gets a label on EVERY instruction and one
    # dispatcher at the end that resolves the target locally, falling back to
    # the global dispatcher for a genuine tail call through a register.
    # Restricted to functions that actually have an indirect JMP -- 1299 of
    # them, 72k instructions -- so nothing else pays for it.
    #
    # A JMP is indirect if the exporter SAID so, or if it simply has no
    # resolved target -- and the second half matters more than the first.
    #
    # Ghidra does not set `ind` on the classic MSVC switch dispatch,
    # `JMP dword ptr [EAX*0x4 + <table>]`: that form arrives with no `ind`
    # and no `flow`. Testing `ind` alone therefore left _has_injmp FALSE for
    # every switch in the image, so the dispatch fell through to the GLOBAL
    # dispatcher -- and a case label, which is code in the middle of this very
    # function, was reported as "no recompiled body at <addr>".
    #
    # That report is what sent three sessions of the discovery loop seeding
    # case labels and carving this function into five pieces (issue #21,
    # C123). The switch tables at 0x005fb240/0x005fb250 in XMen2.exe are the
    # worked example: every entry is an address inside the function that reads
    # them.
    fn["_has_injmp"] = any(i["m"] == "JMP" and (i.get("ind") or "flow" not in i)
                           for i in fn["ins"])
    # Labels another function jumps INTO. The way in is the same offset switch,
    # so owning one is enough on its own to need it -- this function may have
    # no computed jump at all. 0x0066ced2 does not: it is entered at 0x0066cf3c
    # by the switch in the block after it (issue #29).
    fn["_entered_at"] = sorted(a for a, ep in INTERIOR.items()
                               if ep == fn["ep"] and a in fn["_addrs"])
    #
    # The entry point is not always the LOWEST address in the body.
    #
    # MSVC puts an adjustor thunk far from the code it jumps to, and Ghidra
    # merges the two ranges into one function whose ENTRY is the higher
    # address:
    #
    #     005bee90  MOV EDX,[ESP+4]      <- lowest address, NOT the entry
    #     ...
    #     005beeab  JMP 0x005beca0
    #     005d4d80  ADD ECX,0x867ac      <- the entry point
    #     005d4d86  JMP 0x005bee90
    #
    # Emitting the instructions in address order and falling into the first one
    # runs the body from the wrong place. Here that skipped `ADD ECX,0x867ac`,
    # so an array base was never applied, `(ptr - base) / 804` came out as 685
    # instead of 0, and the run faulted on element 685 of a 175-element array --
    # two functions and a vtable dispatch away from the instruction that was
    # skipped (issue #36). It links, it runs, and it is wrong.
    fn["_ep_first"] = fn["ins"][0]["a"] == fn["ep"] if fn["ins"] else True
    if not fn["_ep_first"]:
        if fn["ep"] not in fn["_addrs"]:
            return None, ("the entry point 0x%08x is not one of this "
                          "function's instructions" % fn["ep"])
        targets.add(fn["ep"])
    if fn["_has_injmp"] or fn["_entered_at"]:
        fn["_has_injmp"] = True
        targets |= fn["_addrs"]
    #
    # An instruction this translator does not understand does NOT sink the
    # whole function any more. It is replaced, in place, by a call that stops
    # by name if control ever reaches it.
    #
    # The design rule is unchanged -- an unhandled instruction must fail loudly
    # rather than become a no-op -- but refusing the entire body enforced it far
    # more broadly than the rule requires, and that cost real coverage.
    # Gap::Core::igGetCPUCaps is 898 instructions implementing a 59-case query;
    # ONE of those cases uses SSE to detect SSE, and the engine only ever asks
    # for cases 0 and 1. Refusing the function made the run stop on a case it
    # never executes, while a per-instruction refusal stops only if the SSE
    # case is genuinely reached (issue #17).
    #
    # This is sound because the replacement ABORTS: an unsupported instruction
    # that is never executed cannot affect the state, and one that is executed
    # never returns, so no later instruction runs on state it corrupted.
    #
    unsupported = []
    if fn["_entered_at"]:
        # Entered at a LABEL rather than at the entry: a shared MSVC epilogue
        # reached by a JMP out of another body (issue #29). The way in is the
        # same offset switch a computed jump uses.
        #
        # CLEARED before jumping. A value left set would send the next ORDINARY
        # call to the same label and silently skip this function's prologue --
        # which is the kind of defect that surfaces thousands of instructions
        # later as a stack that does not balance.
        body.append("  /* entered at a label by: %s */"
                    % ", ".join("0x%08x" % a for a in fn["_entered_at"]))
        body.append("  if (C->enter_at) {")
        body.append("    _injmp = C->enter_at; C->enter_at = 0;")
        body.append("    goto L_injmp; }")
    if not fn["_ep_first"]:
        # Ordinary entry, at an address that is not the first instruction. The
        # label is guaranteed above.
        body.append("  /* the entry point 0x%08x is not the lowest address in "
                    "this body */" % fn["ep"])
        body.append("  goto L_%08x;" % fn["ep"])
    for ins in fn["ins"]:
        if ins["a"] in targets:
            body.append("L_%08x:;" % ins["a"])
        if RECORD_RANGES and in_record_range(ins["a"]):
            # BEFORE the instruction, so the line shows the state it ran on.
            body.append("  X86_RECORD(0x%08xU, C, \"%s\");"
                        % (ins["a"], ins["t"].replace("\\", "\\\\")
                                              .replace('"', "'")))
        try:
            body.extend("  " + l for l in emit_instruction(ins, fn))
        except Unsupported as e:
            unsupported.append((ins["a"], ins["m"], str(e)))
            body.append("  /* NOT TRANSLATED: %s -- %s */"
                        % (ins["t"].replace("*/", "* /"), str(e).replace("*/", "* /")))
            body.append("  x86_unsupported_insn(0x%08xU, 0x%08xU, \"%s\", \"%s\");"
                        % (fn["ep"], ins["a"],
                           fn["qname"].replace('"', "'"),
                           str(e).replace('"', "'")))
    if not fn["ins"]:
        return None, "no decoded instructions"
    if fn["_has_injmp"]:
        # Reached only by an explicit goto, so it cannot be fallen into.
        # Switch on the OFFSET from the module base, not the address. The
        # jump table lives in the module's .rdata and its entries are RELOCATED
        # by the loader, so at run time they hold mapped addresses -- 0x2406790e
        # where the table in the file says 0x1006790e -- and a case label
        # cannot contain G_IMGBASE because it is a runtime value. Subtracting it
        # first makes the label a constant again, and a genuine tail call out of
        # this module underflows to something no case matches, so it falls
        # through to the global dispatcher exactly as it should.
        body.append("  if (0) { L_injmp:;")
        body.append("    switch ((uint32_t)(_injmp - G_IMGBASE)) {")
        for a in sorted(fn["_addrs"]):
            body.append("    case 0x%xU: goto L_%08x;" % (a - IMG[0], a))
        body.append("    default: break; }")
        body.append("    DISPATCH(C, _injmp); return; }")
    fn["_unsupported"] = unsupported
    return body, None


# --------------------------------------------------------------- commands

def image_bounds(d):
    """[base, end) of the module's REAL image, excluding Ghidra's synthetic blocks.

    This used to be `max(start + size)` over every block, which is wrong in a
    way that is invisible in the output and corrupts arithmetic rather than
    addresses. Ghidra's export carries a synthetic `tdb` block at 0xffdff000
    (the thread debug block), so the maximum was 0xffe00000 and img_rel() then
    treated EVERY immediate from the image base up to 4 GB as an address into
    this module and rebased it. Measured before the fix: 8,460 operands in
    2,513 functions across the ten exported modules, worst in XMen2.exe (6,400)
    because its base is 0x400000, so almost any large constant qualified.

    The symptom is not a wild pointer -- it is a BIT MASK silently becoming a
    different number. `AND ECX,0x7fffffe1` in igArena_malloc was emitted as
    `AND ECX,(G_IMGBASE + 0x6fffffe1)`, which cleared the wrong bits, left a
    chunk header in its extension form and made the allocator hand out a block
    overlapping its own top chunk (issue #15).

    The real image is the run of blocks contiguous from the base; anything that
    does not join it is not part of this module. Exclusions are RETURNED so the
    caller can print them -- a bounds computation that silently drops a real
    section would be the same class of bug in the other direction.
    """
    base = d["image_base"]
    end = base
    excluded = []
    for b in sorted(d["blocks"], key=lambda b: b["start"]):
        # 64 KB of slack: PE section alignment leaves gaps, but nothing that
        # belongs to the image sits a whole allocation granule away.
        if b["start"] >= base and b["start"] <= end + 0x10000:
            end = max(end, b["start"] + b["size"])
        else:
            excluded.append(b)
    IMG_EXCLUDED[:] = excluded
    return base, end


IMG_EXCLUDED = []


def load(path):
    with open(path) as f:
        d = json.load(f)
    # tools/pe.py iat <dll> > <same-stem>.iat
    iat_path = re.sub(r"\.json$", ".iat", path)
    KNOWN_EPS.clear()
    KNOWN_EPS.update(fn["ep"] for fn in d["functions"])
    IMG[0], IMG[1] = image_bounds(d)
    sys.stderr.write("image 0x%08x-0x%08x (%.2f MB)\n"
                     % (IMG[0], IMG[1], (IMG[1] - IMG[0]) / 1048576.0))
    for b in IMG_EXCLUDED:
        sys.stderr.write("  block NOT part of the image, excluded from the "
                         "address test: %s 0x%08x+0x%x\n"
                         % (b.get("name", "?"), b["start"], b["size"]))
    try:
        with open(iat_path) as f:
            for line in f:
                p = line.split()
                if len(p) == 3 and p[0].startswith("0x"):
                    IAT[int(p[0], 16)] = (p[1], p[2])
    except IOError:
        # Refuse to pretend: without the IAT every import call looks like an
        # unresolvable indirect call and coverage silently collapses.
        sys.exit("recomp: %s missing -- generate it with `pe.py iat <dll>`; "
                 "without it import calls cannot be resolved and the coverage "
                 "number would be meaningless" % iat_path)
    # Import thunks have to be known before any body is emitted: a call to the
    # _setjmp3 thunk is emitted differently from every other call.
    n = len(find_setjmp_thunks(d["functions"]))
    if n:
        sys.stderr.write("%d _setjmp3 import thunk(s) found; calls to them are "
                         "emitted as an inline host setjmp\n" % n)
    # Branch targets inside another function -- shared MSVC epilogues. Known
    # before any body is emitted, because both the jump site and the owner are
    # emitted differently because of them.
    INTERIOR.clear()
    INTERIOR.update(interior_entries(d["functions"]))
    sys.stderr.write("%d address(es) are jumped into from ANOTHER function's "
                     "body (shared epilogues); their owners get an entry "
                     "label\n" % len(INTERIOR))
    return d


def cmd_report(argv):
    d = load(argv[0])
    fns = d["functions"]
    ok, reasons, blocked, dead = 0, Counter(), Counter(), Counter()
    ok_ins = tot_ins = bad_ins = 0
    withbad = 0
    for fn in fns:
        tot_ins += len(fn["ins"])
        body, why = translate(fn)
        if body is None:
            key = re.sub(r"'[^']*'|\"[^\"]*\"|0x[0-9a-f]+", "…", why)
            dead[key] += 1
            continue
        bad = fn.get("_unsupported") or []
        ok += 1
        ok_ins += len(fn["ins"]) - len(bad)
        bad_ins += len(bad)
        if bad:
            withbad += 1
            for _a, _m, whyi in bad:
                key = re.sub(r"'[^']*'|\"[^\"]*\"|0x[0-9a-f]+", "…", whyi)
                reasons[key] += 1
                blocked[key] += 1
    print("program: %s" % d["program"])
    print("functions emitted: %d of %d (%.1f%%)"
          % (ok, len(fns), 100.0 * ok / len(fns) if fns else 0))
    print("instructions translated: %d of %d (%.2f%%)"
          % (ok_ins, tot_ins, 100.0 * ok_ins / tot_ins if tot_ins else 0))
    print("")
    # The unit is now the INSTRUCTION, not the function. An unsupported
    # instruction no longer sinks its whole body -- it is emitted as a call
    # that stops by name if reached -- so the honest measure of the gap is how
    # many instructions are unreachable-or-fatal, and in how many functions.
    print("UNSUPPORTED INSTRUCTIONS: %d, in %d function(s). Each aborts by name"
          % (bad_ins, withbad))
    print("  IF EXECUTED; the rest of those functions runs. Nothing is skipped "
          "silently.")
    for why, n in reasons.most_common(25):
        print("  %5d instr(s)  %s" % (n, why))
    if not reasons:
        print("  (none)")
    if dead:
        print("")
        print("FUNCTIONS NOT EMITTED AT ALL:")
        for why, n in dead.most_common(10):
            print("  %5d fns  %s" % (n, why))


def write_trunc(json_path, program, emitted, skipped):
    """Write the merge-candidate list, and say what it does NOT cover.

    The negative here is the interesting one: "no truncated bodies" must be
    distinguishable from "I only looked at the ones that translated". A
    function the emitter refused has no instruction list to end, so it cannot
    appear -- that blind spot is printed with the count, every time.
    """
    stem = program.rsplit(".", 1)[0] if "." in program else program
    path = os.path.join(os.path.dirname(json_path) or ".", stem + ".trunc")
    with open(path, "w") as f:
        for ep, nxt in sorted(FALL_DEADEND):
            f.write("0x%08x\n" % ep)
    print("truncated bodies: %d of %d emitted function(s) end without a "
          "terminator and fall into code that is no known function; wrote %s "
          "(feed it to `ghidra_export.sh %s --merge`). Blind to the %d "
          "function(s) NOT emitted -- an untranslatable body has no end to "
          "check." % (len(FALL_DEADEND), emitted, path, stem, skipped))
    for ep, nxt in sorted(FALL_DEADEND)[:10]:
        print("  0x%08x runs into 0x%08x" % (ep, nxt))
    if len(FALL_DEADEND) > 10:
        print("  ... and %d more, all in %s" % (len(FALL_DEADEND) - 10, path))


def cmd_emit(argv):
    """Emit the recompiled bodies.

    `--isolate <file>` puts each entry point listed in <file> (hex, one per
    line) into a translation unit of its own, which is what makes a
    `-Wl,--wrap` native override actually fire.

    `--split N` writes N functions per translation unit instead of one giant
    file. XMen2.exe is 13,426 functions -- 72 MB and 2.05 million lines as a
    single unit, which cc1 chews through at 3.3 GB resident and cannot
    parallelise at all. Splitting bounds the memory per process and lets make
    -j use every core, and it costs nothing in generated code: the chunks
    share one prologue and one set of forward declarations, so a call between
    two functions in different chunks is the same call it always was.
    """
    d = load(argv[0])
    set_fn_prefix(d["program"])
    out = argv[1]
    isolate = set()
    # Default to the module's own generated list, if one exists.
    #
    # Three call sites re-emit modules (add_module.sh and two in
    # native_discover.sh) and none of them passed --isolate. Any one of them
    # would have re-emitted XMen2 without isolating the overridden function,
    # which does not fail: it links, and every native override silently stops
    # firing. Making the emitter find the list itself is the only version of
    # this that cannot be forgotten.
    ipath = None
    if "--isolate" in argv:
        ipath = argv[argv.index("--isolate") + 1]
    else:
        stem = d["program"].rsplit(".", 1)[0] if "." in d["program"] \
            else d["program"]
        auto = os.path.join(os.path.dirname(argv[0]), stem + ".isolate")
        if os.path.exists(auto):
            ipath = auto
            print("emit: using %s automatically (native overrides declared in "
                  "src/native/overrides.json)" % auto)
    if ipath:
        try:
            with open(ipath) as f:
                for line in f:
                    line = line.split("#", 1)[0].strip()
                    if line:
                        isolate.add(int(line, 16))
        except IOError as e:
            raise Unsupported("--isolate %s: %s" % (ipath, e))
        if not isolate:
            raise Unsupported(
                "--isolate %s lists no entry points. A native override only "
                "fires if its function is in its own translation unit, so an "
                "empty list would silently link a binary with every override "
                "absent -- refusing rather than emitting one." % ipath)
    # --record LO-HI, repeatable: instrument exactly these guest addresses.
    #
    # It REFUSES a range that matches no instruction rather than emitting a
    # build that records nothing and looks the same as one that does. A range
    # is not a guess -- it is copied from a disassembly listing -- so a range
    # that hits nothing means the address is wrong, and finding that out at
    # emit time costs seconds instead of a build and a run.
    RECORD_RANGES[:] = []
    for i, a in enumerate(argv):
        if a != "--record":
            continue
        spec = argv[i + 1]
        if "-" not in spec:
            raise Unsupported("--record wants LO-HI, got %r" % spec)
        lo, hi = spec.split("-", 1)
        RECORD_RANGES.append((int(lo, 16), int(hi, 16)))
    if RECORD_RANGES:
        hit = [0] * len(RECORD_RANGES)
        for fn in d["functions"]:
            for ins in fn["ins"]:
                for k, (lo, hi) in enumerate(RECORD_RANGES):
                    if lo <= ins["a"] <= hi:
                        hit[k] += 1
        for k, (lo, hi) in enumerate(RECORD_RANGES):
            if not hit[k]:
                raise Unsupported(
                    "--record 0x%08x-0x%08x matches NO instruction in %s. A "
                    "range that records nothing produces a build "
                    "indistinguishable from one with no recording at all, so "
                    "this refuses rather than emitting it."
                    % (lo, hi, d["program"]))
        print("emit: RECORDING %d instruction(s) across %d range(s): %s"
              % (sum(hit), len(RECORD_RANGES),
                 ", ".join("0x%08x-0x%08x (%d)" % (lo, hi, hit[k])
                           for k, (lo, hi) in enumerate(RECORD_RANGES))))

    split = 0
    if "--split" in argv:
        split = int(argv[argv.index("--split") + 1])
        if split < 1:
            raise Unsupported("--split needs a positive function count")
    fns = d["functions"]
    mod_ident = c_mod_ident(d["program"])
    base_sym = "g_imgbase_" + mod_ident
    lines = [PROLOGUE % (d["program"], base_sym, base_sym)]
    # Forward-declare EVERY function, translated or not. A call into an
    # untranslated function must still link -- it gets a body that aborts by
    # name (below), so reaching one is a loud, located failure rather than a
    # link error that tempts you to stub it out silently.
    for fn in fns:
        lines.append("void %s(CPU *C);" % fname(fn["ep"]))
    seen = set()
    for va in sorted(IAT):
        mod, sym = IAT[va]
        ident = c_ident(mod, sym)
        if ident not in seen:
            seen.add(ident)
            lines.append("void %s(CPU *C);   /* %s!%s */" % (ident, mod, sym))
    lines.append("")
    if RECORD_RANGES:
        # The recorded ranges REGISTER THEMSELVES at load. Without this the
        # runtime cannot tell "no region was instrumented" from "the region
        # never ran", which are the two answers a reader most needs apart --
        # and the emitter is the only thing that knows which ranges went in.
        lines.append("void x86_record_range(uint32_t lo, uint32_t hi);")
        lines.append("__attribute__((constructor)) static void "
                     "x86_record_ranges_%s(void) {" % mod_ident)
        for lo, hi in RECORD_RANGES:
            lines.append("  x86_record_range(0x%08xU, 0x%08xU);" % (lo, hi))
        lines.append("}")
        lines.append("")
    hdr_end = len(lines)          # everything above is shared by every chunk
    all_eps = set(f["ep"] for f in fns)
    # Functions Ghidra knows never return. A body ending in a call to one of
    # these is COMPLETE -- there is no fall-through to find, and the analyser
    # stopped where it did on purpose.
    noret_eps = dict((f["ep"], f.get("name") or "0x%08x" % f["ep"])
                     for f in fns if f.get("noret"))
    done = skipped = 0
    bodies = []                   # one list of lines per function
    body_eps = []                 # the ep of bodies[i], for --isolate
    for fn in fns:
        body, why = translate(fn)
        b = []
        if body is None:
            skipped += 1
            b.append("/* NOT TRANSLATED: %s @ 0x%08x -- %s */"
                     % (fn["qname"], fn["ep"], why))
            b.append("void %s(CPU *C) { (void)C; "
                     "x86_untranslated(0x%08xU, \"%s\", \"%s\"); }"
                     % (fname(fn["ep"]), fn["ep"],
                        fn["qname"].replace('"', "'"),
                        why.replace('"', "'")))
            b.append("")
        else:
            b.append("/* %s  @ 0x%08x  (%d instrs) */"
                     % (fn["qname"], fn["ep"], len(fn["ins"])))
            b.append("void %s(CPU *C) {" % fname(fn["ep"]))
            b.append("  const uint32_t _x86_fn_ep = 0x%08xU; (void)_x86_fn_ep;"
                     % fn["ep"])
            b.append("  const uint32_t _retaddr = RD32(C->esp);")
            if fn.get("_has_injmp"):
                b.append("  uint32_t _injmp = 0; (void)_injmp;")
            b.append("  X86_ENTER_FN(_x86_fn_ep);")
            b.extend(body)
            # A body whose last instruction is not a terminator FALLS THROUGH
            # to the next address -- that is what the hardware does, and it is
            # what a function sharing a tail with the next one relies on.
            # Falling off the end of the emitted C instead returns with
            # whatever ESP the partial body left, and the failure lands at some
            # later RET popping the wrong word (issue #13). So the fall-through
            # is emitted explicitly: a tail call to whatever is at the next
            # address, or a named stop if nothing is.
            last = fn["ins"][-1]
            lm = last["m"].upper()
            if not (lm.startswith("RET") or lm.startswith("JMP")
                    or lm in ("INT3", "UD2", "HLT")):
                nxt = last["a"] + last.get("n", 1)
                noret_to = (noret_eps.get(last.get("flow"))
                            if lm == "CALL" else None)
                b.append("  /* falls through to 0x%08x */" % nxt)
                if noret_to is not None:
                    # The body ends at a call that never comes back. Reaching
                    # the next line means OUR implementation of that callee
                    # returned, which is the defect worth naming -- not the
                    # boundary. Six of XMen2.exe's fifteen "truncated" bodies
                    # were this, all ending in longjmp/exit/terminate/
                    # _CxxThrowException, and every merge attempt on them
                    # correctly changed nothing.
                    b.append("  x86_after_noreturn(%s, \"%s\");"
                             % (img_rel(fn["ep"]) or "0x%08xU" % fn["ep"],
                                noret_to.replace('"', "'")))
                elif nxt in all_eps:
                    b.append("  %s(C); return;" % fname(nxt))
                else:
                    # Both MAPPED, for the same reason as x86_int3 (C101).
                    b.append("  x86_fallthrough(%s, %s);"
                             % (img_rel(fn["ep"]) or "0x%08xU" % fn["ep"],
                                img_rel(nxt) or "0x%08xU" % nxt))
                    # A dead-end fall-through is a BOUNDARY DEFECT, found here
                    # and nowhere else: the emitter is the only stage that
                    # knows both where each body ends and what is a known
                    # entry point. It used to be found by RUNNING the game and
                    # reading the abort, one address per crash, and the list of
                    # merge candidates was a file somebody typed by hand -- so
                    # it went stale the first time the module was re-exported.
                    FALL_DEADEND.append((fn["ep"], nxt))
            b.append("}")
            b.append("")
            done += 1
        bodies.append(b)
        body_eps.append(fn["ep"])
        lines.extend(b)
    if not split:
        with open(out, "w") as f:
            f.write("\n".join(lines) + "\n")
        print("%d immediate(s) rewritten as image-relative (G_IMGBASE + off); "
              "a spike here means the image bounds are wrong and BIT MASKS are "
              "being rebased -- see image_bounds()" % IMG_REBASED[0])
        print("emitted %d functions to %s; %d NOT emitted (see `report`)"
              % (done, out, skipped))
        write_trunc(argv[0], d["program"], done, skipped)
        return

    # header = prologue + every forward declaration, repeated in each chunk so
    # a chunk can call into any other without knowing which one holds it.
    head = lines[:hdr_end]
    stem = out[:-2] if out.endswith(".c") else out
    nchunk = 0

    # --isolate: each named function goes in a file of its OWN.
    #
    # ld's --wrap only redirects calls that CROSS an object file. A function
    # sharing a chunk with its caller is bound at compile time, so the override
    # links fine, reports nothing, and never fires -- the Xbox side lost two
    # call sites and a whole observer to exactly this (xbox issue #4). Both
    # halves of an override therefore come from one file (src/native/overrides.json)
    # via tools/gen_overrides.py, so they cannot drift apart by hand.
    iso_done = set()
    rest = []
    for b, ep in zip(bodies, body_eps):
        if ep in isolate:
            nchunk += 1
            with open("%s_%03d.c" % (stem, nchunk - 1), "w") as f:
                f.write("\n".join(head) + "\n")
                f.write("\n".join(b) + "\n")
            iso_done.add(ep)
        else:
            rest.append(b)
    missing = isolate - iso_done
    if missing:
        raise Unsupported(
            "--isolate named %d entry point(s) this module does not emit: %s. "
            "An override on a function that was never emitted cannot fire, and "
            "the link would succeed anyway."
            % (len(missing), ", ".join("0x%08x" % a for a in sorted(missing))))
    if iso_done:
        print("isolated %d function(s) into their own translation unit for "
              "native overrides: %s"
              % (len(iso_done), ", ".join("0x%08x" % a for a in sorted(iso_done))))

    for start in range(0, len(rest), split):
        nchunk += 1
        with open("%s_%03d.c" % (stem, nchunk - 1), "w") as f:
            f.write("\n".join(head) + "\n")
            for b in rest[start:start + split]:
                f.write("\n".join(b) + "\n")
    # The un-split name must not be left behind holding a stale full copy: the
    # build globs, and it would compile both and collide on every symbol.
    if os.path.exists(out):
        os.remove(out)
    print("%d immediate(s) rewritten as image-relative (G_IMGBASE + off); "
          "a spike here means the image bounds are wrong and BIT MASKS are "
          "being rebased -- see image_bounds()" % IMG_REBASED[0])
    print("emitted %d functions to %d chunks %s_NNN.c; %d NOT emitted "
          "(see `report`)" % (done, nchunk, stem, skipped))
    write_trunc(argv[0], d["program"], done, skipped)


# Every libIG*.dll is linked for 0x10000000, so entry points -- and therefore
# emitted function names -- collide across modules. A native binary links them
# all, so the names have to carry their module. Set once per command from the
# JSON, so every generated file for a module agrees.
FN_PREFIX = "fn_"


def fname(ep):
    return "%s%08x" % (FN_PREFIX, ep)


def set_fn_prefix(program):
    global FN_PREFIX
    FN_PREFIX = "fn_%s_" % c_mod_ident(program)
    return FN_PREFIX


def c_mod_ident(program):
    """libIGCore.dll -> libIGCore. Used to give each module its own globals."""
    stem = program.rsplit(".", 1)[0] if "." in program else program
    return "".join(ch if (ch.isalnum() or ch == "_") else "_" for ch in stem)


PROLOGUE = '''/* generated by tools/recomp.py from %s -- do not edit */
#include <stdint.h>

/* This module's OWN image base.
 *
 * Every libIG*.dll in this game is linked for 0x10000000, so at most one of
 * them can sit at its preferred address and the rest are relocated -- by the
 * Windows loader in the hosted build, by pe_map in the native one. Absolute
 * references inside a module are emitted as G_IMGBASE + offset, so each module
 * has to resolve that against ITS OWN base. A single shared g_imgbase silently
 * pointed every module at whichever one loaded last.
 */
extern uint32_t %s;
#define X86_IMGBASE %s

#include "x86rt.h"

'''

def cmd_runtime(argv):
    """Emit the support file: import stubs, dispatch, and the abort paths.

    Import stubs abort by name rather than returning 0. A stub that returns a
    plausible value turns "this import is not implemented yet" into a wrong
    result somewhere downstream, which is exactly the failure mode this project
    cannot afford.
    """
    d = load(argv[0])
    set_fn_prefix(d["program"])
    out = argv[1]
    fns = [fn for fn in d["functions"] if translate(fn)[0] is not None]
    mode = argv[2] if len(argv) > 2 else ""
    nostubs = mode in ("nostubs", "hostimports")
    L = ['/* generated by tools/recomp.py runtime -- do not edit */',
         '#include <windows.h>',
         '#include "x86rt.h"', '#include <stdio.h>', '#include <stdlib.h>',
         '#include <string.h>', '']
    for fn in fns:
        L.append("void %s(CPU *C);" % fname(fn["ep"]))
    L.append("")
    L.append("static const struct { uint32_t ep; void (*fn)(CPU *); "
             "const char *name; } g_fns[] = {")
    for fn in fns:
        L.append('  { 0x%08xU, %s, "%s" },'
                 % (fn["ep"], fname(fn["ep"]), fn["qname"].replace('"', "'")))
    L.append("};")
    L.append("const int g_fn_count = %d;" % len(fns))
    L.append("")
    L.append(RUNTIME_BODY)
    # The hosted build has exactly one module in the process, so its per-module
    # base and the plain global are the same storage under two names. An alias
    # rather than a second variable: two variables can drift, and the failure
    # would be absolute references resolving against a stale base.
    L.append("extern uint32_t g_imgbase_%s __attribute__((alias(\"g_imgbase\")));"
             % c_mod_ident(d["program"]))
    seen = set()
    if not nostubs:
        for va in sorted(IAT):
            mod, sym = IAT[va]
            ident = c_ident(mod, sym)
            if ident in seen:
                continue
            seen.add(ident)
            L.append('void %s(CPU *C) { (void)C; x86_missing_import("%s", "%s"); }'
                     % (ident, mod, sym.replace('"', "'")))
    if mode == "hostimports":
        # Real imports for a TEST binary: same ESP-switching call as the DLL
        # uses, so functions that call into libIGCore can be differentially
        # tested instead of being excluded from verification entirely.
        names, byid = [], {}
        for va in sorted(IAT):
            mod, sym = IAT[va]
            ident = c_ident(mod, sym)
            if ident not in byid:
                byid[ident] = len(names)
                names.append((mod, sym, ident))
        L.append("#include <windows.h>")
        L.append("static void *g_imp[%d];" % max(len(names), 1))
        L.append("static const char *const g_imp_mod[] = {")
        for mod, sym, ident in names:
            L.append('  "%s",' % mod)
        L.append("};")
        L.append("static const char *const g_imp_sym[] = {")
        for mod, sym, ident in names:
            L.append('  "%s",' % sym.replace('"', "'"))
        L.append("};")
        L.append("#define N_IMP %d" % len(names))
        # The mapped image is a copy of the FILE, whose IAT slots still hold
        # hint/name RVAs rather than addresses. Code that calls through the IAT
        # (`MOV EDI,[iat]; CALL EDI` -- the CRT does this immediately) would
        # otherwise jump to an RVA. Patch every slot after resolution.
        L.append("static const struct { uint32_t rva; int imp; } g_iat[] = {")
        n_iat = 0
        for va in sorted(IAT):
            mod, sym = IAT[va]
            L.append("  { 0x%08xU, %d }," % (va - IMG[0], byid[c_ident(mod, sym)]))
            n_iat += 1
        L.append("};")
        L.append("#define N_IAT %d" % n_iat)
        L.append(HOSTIMP_BODY)
        for mod, sym, ident in names:
            if ident in ("imp_MSVCR71_setjmp3", "imp_MSVCR71__setjmp3",
                         "imp_MSVCR71_longjmp"):
                continue
            L.append('void %s(CPU *C) { x86_call_host(C, g_imp[%d], "%s!%s"); }'
                     % (ident, byid[ident], mod, sym.replace('"', "'")))
        seen = set(byid)
    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    print("runtime: %d function-table entries, %d import stubs (%s) -> %s"
          % (len(fns), len(seen), mode or "aborting", out))


RUNTIME_BODY = '''
/* Runtime base of the original module; set by whoever loads it. Defaults to
   the preferred base so a process that gets it needs no special handling. */
uint32_t g_imgbase = 0x10000000U;
/* Bounds of the guest image; anything outside is host code. Set by the loader;
   while zero, every indirect target is treated as guest code (the old
   behaviour), so a loader that forgets to set it fails loudly rather than
   silently calling into arbitrary addresses. */
uint32_t g_image_lo, g_image_hi;

/* Hybrid execution: run original machine code for targets with no recompiled
   body. Off by default so nothing falls back unnoticed; the runner opts in. */
int x86_allow_fallback;
#define X86_FB_MAX 4096
static uint32_t x86_fb[X86_FB_MAX];
static int x86_fb_n;

void x86_note_fallback(uint32_t target)
{
    int i;
    for (i = 0; i < x86_fb_n; i++)
        if (x86_fb[i] == target) return;
    if (x86_fb_n < X86_FB_MAX) x86_fb[x86_fb_n++] = target;
    fprintf(stderr, "x86_fallback: 0x%08x has no recompiled body -- running "
                    "ORIGINAL code\\n", target);
}

void x86_fallback_report(void)
{
    int i;
    fprintf(stderr, "x86: %d distinct addresses ran as ORIGINAL code, not "
                    "recompiled:\\n", x86_fb_n);
    for (i = 0; i < x86_fb_n; i++)
        fprintf(stderr, "    0x%08x\\n", x86_fb[i]);
}

/* Overridable so a TEST binary can skip a trial whose random object produced a
   nonsense vtable pointer, instead of aborting the whole run. The DLL keeps the
   aborting default: in production an unresolved indirect call is a hard error,
   never something to continue past. */
void __attribute__((weak)) x86_dispatch_miss(uint32_t target)
{
    fprintf(stderr, "x86_dispatch: no recompiled function at 0x%08x "
                    "(indirect call target outside the translated set)\\n", target);
    x86_dump_history();
    abort();
}

int __attribute__((weak)) g_dispatch_depth;

#ifdef X86_TRACE_CALLS
uint32_t x86_hist[X86_HIST];
unsigned x86_hist_n;
volatile unsigned long x86_fn_calls;
void x86_dump_history(void)
{
    unsigned i, n = x86_hist_n < X86_HIST ? x86_hist_n : X86_HIST;
    fprintf(stderr, "  last %u recompiled functions entered (most recent "
                    "first):\\n", n);
    for (i = 1; i <= n; i++) {
        uint32_t a = x86_hist[(x86_hist_n - i) & (X86_HIST - 1)];
        int j;
        const char *nm = "?";
        for (j = 0; j < g_fn_count; j++)
            if (g_fns[j].ep == a) { nm = g_fns[j].name; break; }
        fprintf(stderr, "    0x%08x %s\\n", a, nm);
    }
}
#endif

/* A RET whose popped address is not the one the function was entered with.
   Resolve it as a call target and continue there. */
void x86_return_to(CPU *C, uint32_t target, uint32_t fn_ep, uint32_t expected)
{
    int i;
    (void)fn_ep; (void)expected;
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].ep == target) { g_fns[i].fn(C); return; }
    /* Not a function entry: it is a resume point INSIDE a function, which this
       translation unit cannot jump into. Report it rather than continue. */
    fprintf(stderr, "x86_return_to: 0x%08x is not a function entry -- a RET "
                    "redirected into the middle of a function, which the "
                    "current translation cannot express\\n", target);
    x86_dump_history();
    abort();
}

void x86_dispatch(CPU *C, uint32_t target)
{
    int i;
    if (++g_dispatch_depth > 64) { g_dispatch_depth = 0;
                                   x86_dispatch_miss(target); }
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].ep == target) {
            g_fns[i].fn(C); g_dispatch_depth--; return;
        }
    g_dispatch_depth--;
    /* An indirect call through the IAT lands on HOST code -- the CRT does this
       in the first few instructions. Anything outside the guest image is a real
       Windows function and must be called on the guest stack. */
    if (g_image_lo && (target < g_image_lo || target >= g_image_hi)) {
        x86_call_host(C, (void *)(uintptr_t)target, "indirect host call");
        return;
    }
    /* HYBRID EXECUTION. The target is inside the image but has no recompiled
       body -- one of the addresses static analysis never resolved into a
       function. The original image is mapped executable at its correct base, so
       the ORIGINAL machine code can simply be run for it. That keeps the
       program alive instead of aborting, and it is honest only because it is
       LOUD: every distinct fallback address is reported once, and the count is
       the remaining work. Silently falling back would let a binary that is
       mostly original code masquerade as a recompilation. */
    if (g_image_lo && x86_allow_fallback) {
        x86_note_fallback(target);
        x86_call_host(C, (void *)(uintptr_t)target, "original code (not recompiled)");
        return;
    }
    x86_dispatch_miss(target);
}

void x86_untranslated(uint32_t ep, const char *name, const char *reason)
{
    fprintf(stderr, "x86_untranslated: reached 0x%08x %s -- blocked by: %s\\n",
            ep, name, reason);
    abort();
}

void x86_unsupported_insn(uint32_t ep, uint32_t addr, const char *name,
                          const char *reason)
{
    fprintf(stderr, "x86_unsupported_insn: reached 0x%08x inside 0x%08x %s; "
            "translator refusal: %s\\n", addr, ep, name, reason);
    x86_dump_history();
    abort();
}

void x86_int3(uint32_t addr)
{
    fprintf(stderr, "x86_int3: reached compiler trap at 0x%08x; a function "
            "classified noreturn returned\\n", addr);
    x86_dump_history();
    abort();
}

void x86_fallthrough(uint32_t fn_ep, uint32_t next)
{
    fprintf(stderr, "x86_fallthrough: 0x%08x ended without a terminator and "
                    "falls through to 0x%08x, which is not a known function\\n",
            fn_ep, next);
    abort();
}

void x86_after_noreturn(uint32_t fn_ep, const char *callee)
{
    fprintf(stderr, "x86_after_noreturn: 0x%08x ends at a call to %s, which "
                    "never returns -- and it returned. The defect is in this "
                    "port's %s, not in the function's boundaries.\\n",
            fn_ep, callee, callee);
    abort();
}

/* ---- the runtime's private stack --------------------------------------
 *
 * The recompiled bodies keep the guest stack pointer in C->esp and push to it
 * with WR32(C->esp -= 4, …); host functions reached from recompiled code are
 * called with the real esp switched to that same guest pointer. So everything
 * below the entry esp belongs to the GUEST, exactly as it does in the original
 * program.
 *
 * The runtime's own C frames must therefore not be there -- and they were:
 * the entry path's `CPU C` sat 36 bytes below the guest stack pointer, so a guest
 * push of ten words reached it and any host callee with a frame of its own ran
 * straight over it. Measured on the igWindow ARK path: libIGCore's
 * igArkRegister allocates 0x108 bytes and overlapped 196 of the CPU struct's
 * 232, after which control was transferred to the CPU struct itself (the fault
 * EIP was its base address). That is one defect behind the whole family of
 * "recompiled function is entered and never returns" failures, because every
 * member of the family calls a host function.
 *
 * The fix is a stack of its own for the runtime, per thread. Guest pushes then
 * descend into unused stack below the entry point, which is what the original
 * code does with it.
 *
 * Nesting: host code called from recompiled code can call BACK into a
 * recompiled export, and that inner entry must not reuse the outer entry's
 * region. Each entry takes a window; running out of windows is reported and
 * aborts rather than silently overlapping, which is the bug this whole change
 * exists to remove.
 */
#define RT_STACK_SIZE   0x400000u        /* 4 MB per thread */
#define RT_WINDOW       0x040000u        /* 256 KB per nested entry: 16 levels */

static DWORD g_rt_tls = TLS_OUT_OF_INDEXES;
static long  g_rt_deepest;               /* windows in use, high-water mark */

typedef struct RtStack {
    unsigned char *base;
    uint32_t       cur;                  /* esp handed to the next entry */
    int            level;
} RtStack;

/* Allocated on first use by each thread; freed on DLL_THREAD_DETACH. */
static RtStack *rt_stack_for_thread(void)
{
    RtStack *s;
    if (g_rt_tls == TLS_OUT_OF_INDEXES) return NULL;
    s = (RtStack *)TlsGetValue(g_rt_tls);
    if (s) return s;
    s = (RtStack *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->base = (unsigned char *)VirtualAlloc(NULL, RT_STACK_SIZE,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE);
    if (!s->base) { free(s); return NULL; }
    /* Top, 16-byte aligned, with a word of slack so a frame pointer walk off
       the end reads our own memory rather than the next region's. */
    s->cur = (uint32_t)(uintptr_t)(s->base + RT_STACK_SIZE - 32u) & ~15u;
    TlsSetValue(g_rt_tls, s);
    return s;
}

void x86_rt_stack_free(void)
{
    RtStack *s;
    if (g_rt_tls == TLS_OUT_OF_INDEXES) return;
    s = (RtStack *)TlsGetValue(g_rt_tls);
    if (!s) return;
    if (s->base) VirtualFree(s->base, 0, MEM_RELEASE);
    free(s);
    TlsSetValue(g_rt_tls, NULL);
}

int x86_rt_stack_init(void)
{
    g_rt_tls = TlsAlloc();
    return g_rt_tls != TLS_OUT_OF_INDEXES;
}

/* Reserve a window and return the esp to run the entry on. There is no
   "carry on without one" path: running the entry on the guest stack is the
   defect this whole mechanism removes, so failing to get a private stack is a
   stop, named, rather than a silent return to the old behaviour. */
uint32_t x86_rt_stack_take(void)
{
    RtStack *s = rt_stack_for_thread();
    uint32_t esp;
    if (!s) {
        fprintf(stderr, "x86_rt_stack_take: no private runtime stack for this "
                        "thread (allocation failed). Refusing to run a "
                        "recompiled body on the guest stack.\\n");
        abort();
    }
    if (s->cur < (uint32_t)(uintptr_t)s->base + RT_WINDOW) {
        fprintf(stderr, "x86_rt_stack_take: %d nested entries have exhausted "
                        "the %u-byte runtime stack. Refusing to overlap two "
                        "entries' frames -- raise RT_STACK_SIZE.\\n",
                s->level, (unsigned)RT_STACK_SIZE);
        abort();
    }
    esp = s->cur;
    s->cur -= RT_WINDOW;
    if (++s->level > g_rt_deepest) g_rt_deepest = s->level;
    return esp;
}

void x86_rt_stack_give(void)
{
    RtStack *s = rt_stack_for_thread();
    if (!s) return;
    s->cur += RT_WINDOW;
    s->level--;
}

/* Entry from real (host) code into a recompiled body. Lives here because the
   function table does. */
/* External linkage and noinline: the only reference is from the naked
   wrapper's asm, which the compiler cannot see. */
__attribute__((noinline))
uint32_t x86_enter_body(uint32_t ep, uint32_t guest_esp, uint32_t ecx)
{
    CPU C;
    int i;
    memset(&C, 0, sizeof C);
    C.esp = guest_esp;
    C.ecx = ecx;
#ifdef X86_WATCH
    /* Where does this C frame sit relative to the guest stack pointer? The
       answer decides whether a guest PUSH can land on top of live C state, and
       it is a measurement rather than a reading of the code: it is what caught
       the shared stack, and it is what proves the private one is in use. */
    x86_watch_stack(ep, guest_esp, &C, sizeof C);
#endif
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].ep == ep) { g_fns[i].fn(&C); return C.eax; }
    fprintf(stderr, "x86_enter_body: no recompiled body at 0x%08x\\n", ep);
    abort();
}

/* The stack switch.
 *
 * Reached by `jmp` from an export shim, so on entry ESP is exactly the guest's
 * ESP: [esp] is the guest caller's return address and [esp+4…] its arguments.
 * EAX holds the entry point, EDX the stdcall pop count, ECX the guest ECX.
 *
 * The rule this exists to keep: NOTHING that has to survive the body may be
 * stored below the entry ESP. That memory belongs to the guest -- the body
 * pushes into it, and every host function the body calls runs its whole frame
 * there. The first version of the entry path violated it twice over: the CPU
 * struct sat 36 bytes below it (libIGCore's igArkRegister ran its 0x108-byte
 * frame over 196 of the struct's 232 bytes and control ended up executing the
 * struct), and the shim's own return address sat 20 bytes below it, so the
 * shim returned to whatever the last host callee had left there.
 *
 * So: the three scratch words go below the entry ESP and are consumed BEFORE
 * the body runs, and every value needed AFTER it -- the guest ESP, the pop
 * count, the caller's callee-saved registers -- lives on the private stack.
 */
__attribute__((naked)) void x86_enter_tramp(void)
{
    __asm__ __volatile__(
        "pushl %eax\\n\\t"                /* [E-4]  entry point   } consumed */
        "pushl %ecx\\n\\t"                /* [E-8]  guest ecx     } before   */
        "pushl %edx\\n\\t"                /* [E-12] pop count     } the body */
        "call _x86_rt_stack_take\\n\\t"   /* eax = private stack esp */
        "movl %esp, %edx\\n\\t"           /* edx = E-12, the scratch block */
        "movl %eax, %esp\\n\\t"           /* everything below is private */
        "pushl %ebp\\n\\t"
        "pushl %ebx\\n\\t"
        "pushl %esi\\n\\t"
        "pushl %edi\\n\\t"
        "movl (%edx), %eax\\n\\t"
        "pushl %eax\\n\\t"                /* pop count, kept for the return */
        "leal 12(%edx), %eax\\n\\t"       /* eax = E, the guest esp */
        "pushl %eax\\n\\t"
        "pushl 4(%edx)\\n\\t"             /* arg3: guest ecx */
        "pushl %eax\\n\\t"                /* arg2: guest esp */
        "pushl 8(%edx)\\n\\t"             /* arg1: entry point */
        "call _x86_enter_body\\n\\t"
        "addl $12, %esp\\n\\t"
        "pushl %eax\\n\\t"                /* the guest's return value */
        "call _x86_rt_stack_give\\n\\t"
        "popl %eax\\n\\t"
        "popl %edx\\n\\t"                 /* E */
        "popl %ecx\\n\\t"                 /* pop count */
        "popl %edi\\n\\t"
        "popl %esi\\n\\t"
        "popl %ebx\\n\\t"
        "popl %ebp\\n\\t"
        "movl %edx, %esp\\n\\t"           /* back on the guest stack at E */
        "popl %edx\\n\\t"                 /* the guest caller's return address */
        "addl %ecx, %esp\\n\\t"           /* stdcall argument cleanup */
        "jmp *%edx\\n\\t");
}

/* The preemption point's budget (X86_ENTER_FN in x86rt.h fires it in every
   recompiled body). This runtime is the Wine/DLL path, which has no guest
   scheduler -- Windows schedules the real threads -- so the hook only re-arms.
   It is defined rather than compiled out so that the generated bodies are
   IDENTICAL between this path and the native one; a body that differs between
   the two is a body whose evidence does not transfer. */
unsigned long x86_preempt_budget = 20000;
void x86_preempt_now(void) { x86_preempt_budget = 20000; }

/* A call target inside the region Ghidra could not resolve into functions.
   Aborting names the address so it can be added to the analysis, rather than
   letting an unresolved target link to something plausible. */
void x86_call_unknown(CPU *C, uint32_t target)
{
    (void)C;
    fprintf(stderr, "x86_call_unknown: 0x%08x has no identified function\\n",
            target);
    abort();
}

void x87_fault(const char *what)
{
    fprintf(stderr, "%s\\n", what);
    abort();
}

void x86_missing_import(const char *mod, const char *sym)
{
    fprintf(stderr, "x86_missing_import: %s!%s is not implemented\\n", mod, sym);
    abort();
}
'''

def ret_bytes(fn):
    """Bytes the function pops on return, from its own RET imm -- exact, and
    taken from the binary rather than parsed out of a mangled signature."""
    n = 0
    for ins in fn["ins"]:
        if ins["m"].upper() == "RET":
            t = ins["t"].split()
            if len(t) > 1:
                try:
                    n = max(n, _num(t[1]))
                except Unsupported:
                    pass
    return n


def cmd_dll(argv):
    """Emit the interop layer: export shims + import stubs, as a .c and a .def.

    Both directions switch ESP between the host C stack and the guest stack and
    let the REAL callee do its own argument cleanup, then read ESP back. That is
    why no argument counts are needed anywhere: the callee's own `ret N` tells
    us what it popped. Recompiling x86 to run on x86 is what makes this legal --
    the guest stack is a real stack and real code can run on it directly.
    """
    d = load(argv[0])
    set_fn_prefix(d["program"])
    cbase, dbase = argv[1], argv[2]
    fwd = argv[4] if len(argv) > 4 else None
    exports, others = {}, []
    for line in open(argv[3]):
        p_ = line.split()
        if len(p_) < 4 or not p_[1].startswith("0x"):
            continue
        if p_[2] == "CODE":
            exports.setdefault(int(p_[1], 16) + d["image_base"], []).append(p_[3])
        else:
            others.append((p_[3], p_[2]))
    fns = {fn["ep"]: fn for fn in d["functions"]}
    trans = {ep: fn for ep, fn in fns.items() if translate(fn)[0] is not None}
    # RECOMP_ONLY names a file of entry points to actually recompile; everything
    # else is forwarded to the original. Swapping 727 functions at once gives a
    # crash with no bisection handle, so the working mode is a small verified
    # set that grows.
    only = os.environ.get("RECOMP_ONLY", "")
    if only:
        keep = set()
        for line in open(only):
            line = line.strip()
            if line and not line.startswith("#"):
                keep.add(int(line, 16))
        missing = keep - set(trans)
        if missing:
            sys.exit("recomp dll: RECOMP_ONLY lists %d entry points that are "
                     "not translatable: %s" % (len(missing),
                     ", ".join("0x%08x" % m for m in sorted(missing)[:5])))
        trans = {ep: fn for ep, fn in trans.items() if ep in keep}
        print("dll: RECOMP_ONLY -> recompiling %d of %d translatable functions"
              % (len(trans), len(fns)))

    L = ['/* generated by tools/recomp.py dll -- do not edit */',
         '#include <windows.h>', '#include "x86rt.h"', '#include <stdio.h>',
         '#include <stdlib.h>', '']
    for ep in sorted(trans):
        L.append("void %s(CPU *C);" % fname(ep))
    L.append("")
    # ---- imports, resolved by name at load
    names = []
    seen = {}
    for va in sorted(IAT):
        mod, sym = IAT[va]
        ident = c_ident(mod, sym)
        if ident in seen:
            continue
        seen[ident] = len(names)
        names.append((mod, sym, ident))
    L.append("static void *g_imp[%d];" % max(len(names), 1))
    L.append("static const char *const g_imp_mod[] = {")
    for mod, sym, ident in names:
        L.append('  "%s",' % mod)
    L.append("};")
    L.append("static const char *const g_imp_sym[] = {")
    for mod, sym, ident in names:
        L.append('  "%s",' % sym.replace('"', "'"))
    L.append("};")
    L.append("#define N_IMP %d" % len(names))
    L.append(DLL_BODY)
    for mod, sym, ident in names:
        L.append("void %s(CPU *C) { x86_call_host(C, g_imp[%d], \"%s!%s\"); }"
                 % (ident, seen[ident], mod, sym.replace('"', "'")))
    L.append("")
    # ---- export shims
    deflines = ["LIBRARY %s" % d["program"], "EXPORTS"]
    nexp = 0
    for ep, syms in sorted(exports.items()):
        if ep not in trans:
            continue
        pops = ret_bytes(trans[ep])
        # The shim pushes NOTHING that has to outlive the body: everything
        # below the entry ESP belongs to the guest and to any host function the
        # body calls, both of which write there. It hands the entry point and
        # the stdcall pop count to the trampoline in scratch registers and
        # jumps; the trampoline moves all surviving state to the private stack
        # before the body runs. See x86_enter_tramp.
        L.append("__attribute__((naked)) void exp_%08x(void) {" % ep)
        L.append('  __asm__ __volatile__(')
        L.append('    "movl $%d, %%eax\\n\\t"' % ep)
        L.append('    "movl $%d, %%edx\\n\\t"' % pops)
        L.append('    "jmp _x86_enter_tramp\\n\\t");')
        L.append("}")
        for sname in syms:
            deflines.append('  "%s" = exp_%08x' % (sname, ep))
            nexp += 1
    # Anything not translated is FORWARDED to the original DLL, so the
    # replacement is a working hybrid rather than an image the loader rejects.
    # Each forwarder is a piece of the binary we have not recompiled yet, and
    # the count below is the honest remaining-work number.
    nfwd = 0
    if fwd:
        f_ = fwd[:-4] if fwd.lower().endswith(".dll") else fwd
        for ep, syms in sorted(exports.items()):
            if ep in trans:
                continue
            for sname in syms:
                deflines.append('  "%s" = "%s.%s"' % (sname, f_, sname))
                nfwd += 1
        for sname, kind in others:
            deflines.append('  "%s" = "%s.%s"%s'
                            % (sname, f_, sname, " DATA" if kind == "DATA" else ""))
            nfwd += 1
    with open(cbase, "w") as f:
        f.write("\n".join(L) + "\n")
    with open(dbase, "w") as f:
        f.write("\n".join(deflines) + "\n")
    print("dll: %d exports FORWARDED to %s (not yet recompiled)" % (nfwd, fwd))
    print("dll: %d import stubs, %d export shims covering %d exported names -> "
          "%s + %s" % (len(names), len([e for e in exports if e in trans]),
                       nexp, cbase, dbase))
    missing = [e for e in exports if e not in trans]
    if missing:
        print("dll: %d exported entry points are NOT translated and are "
              "therefore NOT exported by the replacement -- the loader will "
              "reject it if anything imports them" % len(missing))


DLL_BODY = '''
/* Run a real (host) function using the GUEST stack, then read ESP back so the
   callee's own cleanup determines the new guest ESP. No signature needed. */
void x86_call_host(CPU *C, void *fn, const char *what)
{
    uint32_t eax, after, gsp = C->esp + 4;   /* +4: drop our fake return addr */
    if (!fn) {
        fprintf(stderr, "x86_call_host: %s unresolved\\n", what);
        abort();
    }
#ifdef X86_WATCH
    x86_watch_note(1, (uint32_t)(uintptr_t)fn, C->esp);
#endif
    __asm__ __volatile__(
        "movl %%esp, %%edi\\n\\t"
        "movl %[g], %%esp\\n\\t"
        "movl %[c], %%ecx\\n\\t"
        "call *%[f]\\n\\t"
        "movl %%esp, %[aft]\\n\\t"
        "movl %%edi, %%esp\\n\\t"
        : "=a"(eax), [aft] "=r"(after)
        : [f] "r"(fn), [g] "r"(gsp), [c] "r"(C->ecx)
        : "ecx", "edx", "edi", "memory");
    C->eax = eax;
    C->esp = after;
#ifdef X86_WATCH
    x86_watch_note(2, (uint32_t)(uintptr_t)fn, after);
#endif
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    int i;
    (void)h; (void)r;
    /* Each thread that enters recompiled code gets a private runtime stack;
       give it back when the thread goes away rather than leaking 4 MB per
       thread for the life of the process. */
    if (reason == DLL_THREAD_DETACH) { x86_rt_stack_free(); return TRUE; }
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    if (!x86_rt_stack_init()) {
        /* Without it every entry would run its C frames on the guest stack,
           where guest pushes and host callees overwrite them. Refuse to load
           rather than reproduce the defect silently. */
        fprintf(stderr, "recomp: TlsAlloc failed -- no private runtime stack, "
                        "refusing to load\\n");
        return FALSE;
    }
    {   /* absolute references into the original image are relative to wherever
           the loader actually put it -- inside the game it is relocated */
        HMODULE o = GetModuleHandleA("libIGDisplay_orig.dll");
        if (!o) o = LoadLibraryA("libIGDisplay_orig.dll");
        if (!o) { fprintf(stderr, "recomp: libIGDisplay_orig.dll absent\\n");
                  return FALSE; }
        g_imgbase = (uint32_t)(uintptr_t)o;
    }
#ifdef X86_WATCH
    /* Before any game code runs, so a failing watch is the FIRST thing in the
       log rather than a conclusion drawn from it. */
    x86_watch_selftest();
    /* And before the imports resolve: a fault during resolution should be
       reported by us, not left as a silent process death. */
    x86_fault_install();
#endif
    for (i = 0; i < N_IMP; i++) {
        HMODULE m = LoadLibraryA(g_imp_mod[i]);
        g_imp[i] = m ? (void *)GetProcAddress(m, g_imp_sym[i]) : NULL;
        if (!g_imp[i]) {
            /* Fail the load rather than leaving a stub that aborts later at a
               point with no connection to the missing import. */
            fprintf(stderr, "recomp: unresolved import %s!%s\\n",
                    g_imp_mod[i], g_imp_sym[i]);
            return FALSE;
        }
    }
    return TRUE;
}
'''

HOSTIMP_BODY = '''
#include <stdlib.h>

/* setjmp cannot be delegated to MSVCR71: the host frame it must resume is the
   generated CALL SITE, not an import wrapper that has already returned. */
#define HOST_JMP_MAX 4096
typedef struct {
    uint32_t env;
    CPU regs;
    jmp_buf buf;
    int used;
} HostJmp;
static HostJmp *g_host_jmp;
static int g_host_jmp_cap;
static int g_host_jmp_active = -1;

static int host_jmp_slot(uint32_t env)
{
    int i, old;
    for (i = 0; i < g_host_jmp_cap; i++)
        if (g_host_jmp[i].used && g_host_jmp[i].env == env) return i;
    for (i = 0; i < g_host_jmp_cap; i++)
        if (!g_host_jmp[i].used) return i;
    old = g_host_jmp_cap;
    if (old >= HOST_JMP_MAX) {
        fprintf(stderr, "x2run: all %d conservative setjmp slots are live; "
                "refusing to overwrite one\\n", old);
        abort();
    }
    g_host_jmp_cap = old ? old * 2 : 16;
    g_host_jmp = (HostJmp *)realloc(g_host_jmp,
                                    (size_t)g_host_jmp_cap * sizeof *g_host_jmp);
    if (!g_host_jmp) { fprintf(stderr, "x2run: no memory for setjmp table\\n"); abort(); }
    memset(g_host_jmp + old, 0,
           (size_t)(g_host_jmp_cap - old) * sizeof *g_host_jmp);
    return old;
}

jmp_buf *x86_setjmp_buf(CPU *C)
{
    uint32_t env = RD32(C->esp + 4u);
    int i = host_jmp_slot(env);
    g_host_jmp[i].env = env;
    g_host_jmp[i].regs = *C;
    g_host_jmp[i].used = 1;
    if (env) WR32(env, 0x53544f50u);
    return &g_host_jmp[i].buf;
}

void x86_setjmp_done(CPU *C, int rc)
{
    if (rc) {
        if (g_host_jmp_active < 0) {
            fprintf(stderr, "x2run: setjmp resumed without a recorded slot\\n");
            abort();
        }
        *C = g_host_jmp[g_host_jmp_active].regs;
        g_host_jmp_active = -1;
    }
    C->eax = (uint32_t)rc;
    C->esp += 4u;
}

void imp_MSVCR71_longjmp(CPU *C)
{
    uint32_t env = RD32(C->esp + 4u), value = RD32(C->esp + 8u);
    int i;
    for (i = 0; i < g_host_jmp_cap; i++)
        if (g_host_jmp[i].used && g_host_jmp[i].env == env) {
            g_host_jmp_active = i;
            longjmp(g_host_jmp[i].buf, value ? (int)value : 1);
        }
    fprintf(stderr, "x2run: longjmp buffer 0x%08x was never recorded\\n", env);
    abort();
}

void imp_MSVCR71_setjmp3(CPU *C)
{
    fprintf(stderr, "x2run: _setjmp3 reached through an import wrapper; no "
            "live generated frame can be captured\\n");
    (void)C;
    abort();
}
void imp_MSVCR71__setjmp3(CPU *C) { imp_MSVCR71_setjmp3(C); }

void x86_call_host(CPU *C, void *fn, const char *what)
{
    uint32_t eax, after, gsp = C->esp + 4;   /* +4: drop our fake return addr */
    if (!fn) { fprintf(stderr, "x86_call_host: %s unresolved\\n", what); abort(); }
#ifdef X86_WATCH
    x86_watch_note(1, (uint32_t)(uintptr_t)fn, C->esp);
#endif
    __asm__ __volatile__(
        "movl %%esp, %%edi\\n\\t"
        "movl %[g], %%esp\\n\\t"
        "movl %[c], %%ecx\\n\\t"
        "call *%[f]\\n\\t"
        "movl %%esp, %[aft]\\n\\t"
        "movl %%edi, %%esp\\n\\t"
        : "=a"(eax), [aft] "=r"(after)
        : [f] "r"(fn), [g] "r"(gsp), [c] "r"(C->ecx)
        : "ecx", "edx", "edi", "memory");
    C->eax = eax;
    C->esp = after;
#ifdef X86_WATCH
    x86_watch_note(2, (uint32_t)(uintptr_t)fn, after);
#endif
}

/* Resolve every import; returns 0 on success, else the count unresolved.
   Refuses to leave a NULL entry that would abort later at an unrelated point. */
int x86_resolve_imports(void)
{
    int i, bad = 0;
    for (i = 0; i < N_IMP; i++) {
        HMODULE m = LoadLibraryA(g_imp_mod[i]);
        const char *sym = g_imp_sym[i];
        /* "@N" is an import BY ORDINAL: GetProcAddress takes the ordinal as an
           integer cast to a pointer, not as the literal string "@N". WS2_32 and
           OLEAUT32 are imported this way here. */
        if (sym[0] == '@') {
            unsigned long ord = strtoul(sym + 1, NULL, 10);
            g_imp[i] = m ? (void *)GetProcAddress(m, (LPCSTR)(uintptr_t)ord) : NULL;
        } else {
            g_imp[i] = m ? (void *)GetProcAddress(m, sym) : NULL;
        }
        if (!g_imp[i]) {
            fprintf(stderr, "unresolved import %s!%s\\n", g_imp_mod[i], sym);
            bad++;
        }
    }
    if (!bad) {
        int k;
        for (k = 0; k < N_IAT; k++)
            *(void **)(uintptr_t)(g_imgbase + g_iat[k].rva) = g_imp[g_iat[k].imp];
        fprintf(stderr, "x86: patched %d IAT slots\\n", N_IAT);
    }
    return bad;
}
'''


NATIVE_BODY_HEAD = """
/* ---- this module's registration ---------------------------------------
 *
 * One binary now links several recompiled modules, so nothing here may be a
 * process-wide singleton. Each module owns its base, its function table and
 * its import stubs; the shared dispatcher in src/native/x86rt_native.c finds
 * the right one by ADDRESS.
 *
 * Dispatch has to key on the mapped address, not the guest entry point: every
 * libIG*.dll is linked for 0x10000000, so entry points collide across modules
 * and a table keyed on them would silently answer with the wrong module's
 * function.
 */
uint32_t X86_IMGBASE;
"""


NATIVE_BODY_TAIL = """
static X86Module g_this_module = {
    /* size is 0 here ON PURPOSE: only the host knows where and how large the
       image actually got mapped, and it fills this in. Emitting a guess is how
       this field first carried 0xefe00000 -- Ghidra reports a block near the
       top of the address space, so "highest block minus image base" was not
       the image size, and every dispatch lookup silently missed. */
    "%(program)s", &X86_IMGBASE, %(preferred)#010xU, 0U,
    g_fns, %(nfns)d, g_imports,
    (int)(sizeof g_imports / sizeof g_imports[0]), NULL
};

/* Registered before main() so a module cannot be linked in and then forgotten;
   the alternative is an init call the host has to remember for each one. */
__attribute__((constructor)) static void x86_register_this_module(void)
{
    x86_module_register(&g_this_module);
}
"""


def cmd_native(argv):
    """Emit one module's NATIVE side: its image base, its function table, its
    registration constructor, and a weak aborting stub per import.

    The dispatcher and the abort paths are NOT emitted here -- they are shared
    across modules and live in src/native/x86rt_native.c. Emitting them per
    module is what a single-module build could get away with; a binary linking
    several would get one dispatcher per module, each seeing only its own
    functions.
    """
    d = load(argv[0])
    set_fn_prefix(d["program"])
    out = argv[1]
    # EVERY function, not just the translatable ones. An untranslatable
    # function still HAS a body -- one that aborts naming the mnemonic that
    # blocked it -- and leaving it out of the dispatch table means reaching it
    # reports "no recompiled body at 0x…", which reads as a missing function
    # rather than as a known translator gap. Measured: libIGSg's entry point
    # looked undetected when it was merely untranslated.
    fns = d["functions"]
    mod_ident = c_mod_ident(d["program"])
    L = ['/* generated by tools/recomp.py native -- do not edit */',
         '#include <stdint.h>',
         'extern uint32_t g_imgbase_%s;' % mod_ident,
         '#define X86_IMGBASE g_imgbase_%s' % mod_ident,
         '#include "x86rt.h"', '#include "x86rt_native.h"',
         '#include <stdio.h>', '#include <stdlib.h>',
         '#include <string.h>', '']
    L.append(NATIVE_BODY_HEAD)
    for fn in fns:
        L.append("void %s(CPU *C);" % fname(fn["ep"]))
    L.append("")
    # An address jumped into from another body is also a DISPATCH target: an
    # indirect jump can compute one just as a direct one can name it. Each gets
    # a shim that sets the entry label and calls the owner, so the dispatcher
    # resolves it exactly like a function -- which is what the boundary surgery
    # around 0x005fac10 was standing in for across three sessions (issue #29).
    for tgt in sorted(INTERIOR):
        L.append("static void %s_at_%08x(CPU *C) { C->enter_at = "
                 "X86_IMGBASE + 0x%xU; %s(C); }"
                 % (fname(INTERIOR[tgt]), tgt, tgt - IMG[0],
                    fname(INTERIOR[tgt])))
    if INTERIOR:
        L.append("")
    L.append("static const X86Fn g_fns[] = {")
    for fn in fns:
        L.append('  { 0x%08xU, %s, "%s" },'
                 % (fn["ep"], fname(fn["ep"]), fn["qname"].replace('"', "'")))
    for tgt in sorted(INTERIOR):
        L.append('  { 0x%08xU, %s_at_%08x, "%s (entered at 0x%08x)" },'
                 % (tgt, fname(INTERIOR[tgt]), tgt,
                    next(f["qname"] for f in fns
                         if f["ep"] == INTERIOR[tgt]).replace('"', "'"), tgt))
    L.append("};")
    L.append("")
    seen_decl = set()
    # Every import slot with the stub that serves it. The host needs this to
    # bind a slot whose target is NOT another recompiled module but IS
    # implemented natively: the guest sometimes takes an import's ADDRESS from
    # the IAT and calls through it, which never reaches the named stub.
    # Declared first: the table names every stub, and the definitions come
    # after it.
    for va in sorted(IAT):
        mod, sym = IAT[va]
        ident = c_ident(mod, sym)
        if ident not in seen_decl:
            seen_decl.add(ident)
            L.append("void %s(CPU *C);" % ident)
    L.append("")
    L.append("static const X86Import g_imports[] = {")
    for va in sorted(IAT):
        mod, sym = IAT[va]
        L.append('  { 0x%08xU, %s, "%s", "%s" },'
                 % (va - IMG[0], c_ident(mod, sym), mod,
                    sym.replace('"', "'")))
    L.append("};")
    L.append("")
    L.append(NATIVE_BODY_TAIL % dict(program=d["program"],
                                     preferred=d["image_base"],
                                     nfns=len(fns)))
    seen = set()
    for va in sorted(IAT):
        mod, sym = IAT[va]
        ident = c_ident(mod, sym)
        if ident in seen:
            continue
        seen.add(ident)
        # Dispatch through this import's own IAT slot, which the host binds
        # at startup exactly as a loader would. That is what makes a call into
        # another RECOMPILED module work: the slot holds the target's mapped
        # address, and x86_import_call runs the body there. If nothing could
        # bind the slot it holds a poison address, and the call is reported
        # with the module and symbol rather than as a bad address.
        #
        # WEAK, so a real native implementation (src/native/win32_sdl.c)
        # overrides it just by existing. A hand-maintained skip list would have
        # to be kept in step with that file, and getting it wrong yields a
        # silently-preferred stub.
        L.append('__attribute__((weak)) void %s(CPU *C) '
                 '{ x86_import_call(C, X86_IMGBASE + 0x%xU, "%s", "%s"); }'
                 % (ident, va - IMG[0], mod, sym.replace('"', "'")))
    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    n_untr = sum(1 for fn in fns if translate(fn)[0] is None)
    print("native: %d function-table entries -- %d function(s), %d of them "
          "untranslated (and they say so when called), plus %d interior entry "
          "point(s) into a shared epilogue; %d imports stubbed WEAK to abort "
          "by name (a native implementation overrides one by existing) -> %s"
          % (len(fns) + len(INTERIOR), len(fns), n_untr, len(INTERIOR),
             len(seen), out))


CMDS = {"report": cmd_report, "emit": cmd_emit, "runtime": cmd_runtime,
        "dll": cmd_dll, "native": cmd_native}

if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in CMDS:
        sys.exit(__doc__)
    CMDS[sys.argv[1]](sys.argv[2:])
