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
            "longdouble": 10}


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
    if up in REG16:
        return Operand("reg16", reg=up)
    if up in LOW:
        return Operand("reg8lo", reg=up)
    if up in HIGH:
        return Operand("reg8hi", reg=up)

    m = re.match(r"^(byte|word|dword|qword|float\d*|double|tbyte|longdouble"
                 r"|undefined\d*)\s+ptr\s+(.*)$", tok, re.I)
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

    # FS-relative access is MSVC's SEH prologue (MOV EAX,FS:[0] etc.). We run
    # as a real 32-bit PE, so FS still addresses the TIB and the exception chain
    # there is the genuine one -- read/write it directly rather than model it.
    m2 = re.match(r"^(FS|GS):(.*)$", tok, re.I)
    if m2:
        seg = m2.group(1).upper()
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
KNOWN_EPS = set()  # entry points Ghidra identified; a call to anything else is
                   # a target inside the region it could not resolve into
                   # functions, and must not be emitted as a direct C call


def img_rel(val):
    """An absolute address inside the module's own image is emitted relative to
    the module's RUNTIME base, not its preferred base.

    This is not cosmetic. The original DLL only loads at 0x10000000 when nothing
    else claims that address; inside the game it is relocated (observed at
    0x001C0000), and every hardcoded 0x100xxxxx reference then reads unrelated
    memory -- silently, because the address is still mapped. The difftest passed
    only because in that small process the DLL did get its preferred base."""
    if IMG[0] <= val < IMG[1]:
        return "(G_IMGBASE + 0x%xU)" % (val - IMG[0])
    return None


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

    if m == "PUSH":
        s = O(0)
        if s.width != 4:
            raise Unsupported("PUSH width %d" % s.width)
        return [A, "C->esp -= 4; WR32(C->esp, %s);" % s.read()]

    if m == "POP":
        d = O(0)
        if d.width != 4:
            raise Unsupported("POP width %d" % d.width)
        return [A, d.write("RD32(C->esp)"), "C->esp += 4;"]

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
             "FYL2X", "FYL2XP1", "FPATAN", "F2XM1", "FSIN", "FCOS",
             "FSINCOS", "FTST",
             "FFREE", "FINCSTP", "FDECSTP", "FNCLEX", "FNINIT"):
        def fsrc(o, integer):
            if o.kind == "mem":
                if integer:
                    return "RDI32(%s)" % o.addr()
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
            return [A, "C->top = 0; C->depth = 0; C->fsw = 0;"]
        if m in ("FNSTCW", "FLDCW"):
            o = O(0)
            if o.kind != "mem":
                raise Unsupported("%s with %s operand" % (m, o.kind))
            if m == "FNSTCW":
                return [A, "WR16(%s, (uint16_t)C->fcw);" % o.addr()]
            return [A, "C->fcw = RD16(%s);" % o.addr()]
        if m == "FNSTSW":
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
                return [A, "X87_ST(C, %s) = x87_pop(C);" % st.group(1)]
            o = O(0)
            if o.kind != "mem":
                raise Unsupported("%s to %s" % (m, o.kind))
            pop = m in ("FSTP", "FISTP")
            val = "x87_pop(C)" if pop else "X87_ST(C, 0)"
            if m in ("FISTP", "FIST"):
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

    if m in ("ADC", "SBB"):
        d, s2 = reconcile(O(0), O(1))
        w = d.width
        op = "+" if m == "ADC" else "-"
        return [A,
                "{ uint32_t _a = %s, _b = %s, _c = FLAG_C(C) ? 1U : 0U, _r;"
                % (d.read(), s2.read()),
                "  _r = _a %s _b %s _c;" % (op, op),
                "  SETFLAGS(C, %s, _a, (uint32_t)(_b %s _c), _r, %d);"
                % ("FK_ADD" if m == "ADC" else "FK_SUB", op, w),
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

    # MMX: registers modelled separately from the x87 stack (see x86rt.h).
    if m in ("MOVQ", "MOVD", "EMMS", "FEMMS"):
        if m in ("EMMS", "FEMMS"):
            return [A, "/* %s: MMX/x87 aliasing not modelled; see x86rt.h */" % m]
        def mmx(tok):
            mm = re.fullmatch(r"MM(\d)", tok.strip().upper())
            return mm.group(1) if mm else None
        a_, b_ = ops[0].strip(), ops[1].strip()
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
        if ops:
            n = parse_operand(ops[0])
            if n.kind != "imm":
                raise Unsupported("RET with non-immediate")
            return [A, "C->esp += 4 + %d; return;" % n.val]
        return [A, "C->esp += 4; return;"]

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
                if m == "CALL":
                    return [A, "C->esp -= 4; WR32(C->esp, 0x%xU);" % ret, call]
                return [A, call, "return;"]     # tail-jump thunk
            # otherwise it is real indirect dispatch (vtable etc.)
            ret = ins["a"] + ins["n"]
            if m == "CALL":
                return [A,
                        "C->esp -= 4; WR32(C->esp, 0x%xU);" % ret,
                        "DISPATCH(C, %s);" % t.read()]
            return [A, "DISPATCH(C, %s); return;" % t.read()]
        if t is not None and t.kind in ("reg32",):
            ret = ins["a"] + ins["n"]
            if m == "CALL":
                return [A,
                        "C->esp -= 4; WR32(C->esp, 0x%xU);" % ret,
                        "DISPATCH(C, %s);" % t.read()]
            return [A, "DISPATCH(C, %s); return;" % t.read()]

    # A JMP whose target lies outside this function is a TAIL CALL, not a
    # branch -- MSVC emits these for one-line wrappers. Treating it as a goto
    # produces `label used but not defined`, which is how it was caught.
    if m == "JMP":
        if ins.get("ind") or "flow" not in ins:
            raise Unsupported("indirect JMP")
        if ins["flow"] not in ctx["_addrs"]:
            if KNOWN_EPS and ins["flow"] not in KNOWN_EPS:
                return [A, "x86_call_unknown(C, 0x%08xU); return;" % ins["flow"]]
            return [A, "fn_%08x(C); return;" % ins["flow"]]
        return [A, "goto L_%08x;" % ins["flow"]]

    if m.startswith("J") and m[1:] in CC:
        if "flow" not in ins:
            raise Unsupported("conditional jump with no resolved target")
        if ins["flow"] not in ctx["_addrs"]:
            if KNOWN_EPS and ins["flow"] not in KNOWN_EPS:
                return [A, "if (%s) { x86_call_unknown(C, 0x%08xU); return; }"
                        % (CC[m[1:]], ins["flow"])]
            return [A, "if (%s) { fn_%08x(C); return; }" % (CC[m[1:]], ins["flow"])]
        return [A, "if (%s) goto L_%08x;" % (CC[m[1:]], ins["flow"])]

    if m == "CALL":
        if ins.get("ind") or "flow" not in ins:
            raise Unsupported("indirect CALL")
        ret = ins["a"] + ins["n"]
        if KNOWN_EPS and ins["flow"] not in KNOWN_EPS:
            # Ghidra did not identify a function at this target. Emitting
            # fn_<addr> would simply fail to link; routing it through the
            # runtime keeps the module buildable and makes reaching it a
            # located, named failure instead of a silent one.
            return [A,
                    "C->esp -= 4; WR32(C->esp, 0x%xU);" % ret,
                    "x86_call_unknown(C, 0x%08xU);" % ins["flow"]]
        return [A,
                "C->esp -= 4; WR32(C->esp, 0x%xU);" % ret,
                "fn_%08x(C);" % ins["flow"]]

    raise Unsupported("mnemonic %s" % m)


def translate(fn):
    """-> (list of C lines, None) or (None, reason)."""
    body = []
    fn["_addrs"] = set(i["a"] for i in fn["ins"])
    targets = set()
    for ins in fn["ins"]:
        if "flow" in ins and ins["m"].upper().startswith("J"):
            if ins["flow"] in fn["_addrs"]:
                targets.add(ins["flow"])
    try:
        for ins in fn["ins"]:
            if ins["a"] in targets:
                body.append("L_%08x:;" % ins["a"])
            body.extend("  " + l for l in emit_instruction(ins, fn))
    except Unsupported as e:
        return None, str(e)
    if not fn["ins"]:
        return None, "no decoded instructions"
    return body, None


# --------------------------------------------------------------- commands

def load(path):
    with open(path) as f:
        d = json.load(f)
    # tools/pe.py iat <dll> > <same-stem>.iat
    iat_path = re.sub(r"\.json$", ".iat", path)
    KNOWN_EPS.clear()
    KNOWN_EPS.update(fn["ep"] for fn in d["functions"])
    IMG[0] = d["image_base"]
    IMG[1] = max((b["start"] + b["size"]) for b in d["blocks"])
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
    return d


def cmd_report(argv):
    d = load(argv[0])
    fns = d["functions"]
    ok, reasons, blocked = 0, Counter(), Counter()
    ok_ins = tot_ins = 0
    for fn in fns:
        tot_ins += len(fn["ins"])
        body, why = translate(fn)
        if body is not None:
            ok += 1
            ok_ins += len(fn["ins"])
        else:
            key = re.sub(r"'[^']*'|\"[^\"]*\"|0x[0-9a-f]+", "…", why)
            reasons[key] += 1
            blocked[key] += len(fn["ins"])
    print("program: %s" % d["program"])
    print("functions fully translatable: %d of %d (%.1f%%)"
          % (ok, len(fns), 100.0 * ok / len(fns) if fns else 0))
    print("instructions in those functions: %d of %d (%.1f%%)"
          % (ok_ins, tot_ins, 100.0 * ok_ins / tot_ins if tot_ins else 0))
    print("")
    print("BLOCKERS, by functions blocked (each is a real gap, not a rounding "
          "error -- nothing is silently skipped):")
    for why, n in reasons.most_common(25):
        print("  %5d fns  %7d instrs  %s" % (n, blocked[why], why))
    if not reasons:
        print("  (none)")


def cmd_emit(argv):
    d = load(argv[0])
    out = argv[1]
    fns = d["functions"]
    lines = [PROLOGUE % d["program"]]
    # Forward-declare EVERY function, translated or not. A call into an
    # untranslated function must still link -- it gets a body that aborts by
    # name (below), so reaching one is a loud, located failure rather than a
    # link error that tempts you to stub it out silently.
    for fn in fns:
        lines.append("void fn_%08x(CPU *C);" % fn["ep"])
    seen = set()
    for va in sorted(IAT):
        mod, sym = IAT[va]
        ident = c_ident(mod, sym)
        if ident not in seen:
            seen.add(ident)
            lines.append("void %s(CPU *C);   /* %s!%s */" % (ident, mod, sym))
    lines.append("")
    done = skipped = 0
    for fn in fns:
        body, why = translate(fn)
        if body is None:
            skipped += 1
            lines.append("/* NOT TRANSLATED: %s @ 0x%08x -- %s */"
                         % (fn["qname"], fn["ep"], why))
            lines.append("void fn_%08x(CPU *C) { (void)C; "
                         "x86_untranslated(0x%08xU, \"%s\", \"%s\"); }"
                         % (fn["ep"], fn["ep"], fn["qname"].replace('"', "'"),
                            why.replace('"', "'")))
            lines.append("")
            continue
        lines.append("/* %s  @ 0x%08x  (%d instrs) */"
                     % (fn["qname"], fn["ep"], len(fn["ins"])))
        lines.append("void fn_%08x(CPU *C) {" % fn["ep"])
        lines.extend(body)
        lines.append("}")
        lines.append("")
        done += 1
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print("emitted %d functions to %s; %d NOT emitted (see `report`)"
          % (done, out, skipped))


PROLOGUE = '''/* generated by tools/recomp.py from %s -- do not edit */
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
    out = argv[1]
    fns = [fn for fn in d["functions"] if translate(fn)[0] is not None]
    mode = argv[2] if len(argv) > 2 else ""
    nostubs = mode in ("nostubs", "hostimports")
    L = ['/* generated by tools/recomp.py runtime -- do not edit */',
         '#include "x86rt.h"', '#include <stdio.h>', '#include <stdlib.h>',
         '#include <string.h>', '']
    for fn in fns:
        L.append("void fn_%08x(CPU *C);" % fn["ep"])
    L.append("")
    L.append("static const struct { uint32_t ep; void (*fn)(CPU *); "
             "const char *name; } g_fns[] = {")
    for fn in fns:
        L.append('  { 0x%08xU, fn_%08x, "%s" },'
                 % (fn["ep"], fn["ep"], fn["qname"].replace('"', "'")))
    L.append("};")
    L.append("const int g_fn_count = %d;" % len(fns))
    L.append("")
    L.append(RUNTIME_BODY)
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
        L.append(HOSTIMP_BODY)
        for mod, sym, ident in names:
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

/* Overridable so a TEST binary can skip a trial whose random object produced a
   nonsense vtable pointer, instead of aborting the whole run. The DLL keeps the
   aborting default: in production an unresolved indirect call is a hard error,
   never something to continue past. */
void __attribute__((weak)) x86_dispatch_miss(uint32_t target)
{
    fprintf(stderr, "x86_dispatch: no recompiled function at 0x%08x "
                    "(indirect call target outside the translated set)\\n", target);
    abort();
}

int __attribute__((weak)) g_dispatch_depth;

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
    x86_dispatch_miss(target);
}

void x86_untranslated(uint32_t ep, const char *name, const char *reason)
{
    fprintf(stderr, "x86_untranslated: reached 0x%08x %s -- blocked by: %s\\n",
            ep, name, reason);
    abort();
}

/* Entry from real (host) code into a recompiled body. Lives here because the
   function table does. */
uint32_t x86_enter(uint32_t ep, uint32_t guest_esp, uint32_t ecx)
{
    CPU C;
    int i;
    memset(&C, 0, sizeof C);
    C.esp = guest_esp;
    C.ecx = ecx;
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].ep == ep) { g_fns[i].fn(&C); return C.eax; }
    fprintf(stderr, "x86_enter: no recompiled body at 0x%08x\\n", ep);
    abort();
}

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
        L.append("void fn_%08x(CPU *C);" % ep)
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
        L.append("__attribute__((naked)) void exp_%08x(void) {" % ep)
        L.append('  __asm__ __volatile__(')
        L.append('    "pushl %ebp\\n\\t"')
        L.append('    "movl %esp, %ebp\\n\\t"')
        L.append('    "pushl %ecx\\n\\t"')          # this
        L.append('    "leal 4(%ebp), %eax\\n\\t"')  # guest esp = &retaddr
        L.append('    "pushl %eax\\n\\t"')
        L.append('    "pushl $%d\\n\\t"' % ep)
        L.append('    "call _x86_enter\\n\\t"')
        L.append('    "addl $12, %esp\\n\\t"')
        L.append('    "leave\\n\\t"')
        L.append('    "ret $%d\\n\\t");' % pops)
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
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    int i;
    (void)h; (void)r;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    {   /* absolute references into the original image are relative to wherever
           the loader actually put it -- inside the game it is relocated */
        HMODULE o = GetModuleHandleA("libIGDisplay_orig.dll");
        if (!o) o = LoadLibraryA("libIGDisplay_orig.dll");
        if (!o) { fprintf(stderr, "recomp: libIGDisplay_orig.dll absent\\n");
                  return FALSE; }
        g_imgbase = (uint32_t)(uintptr_t)o;
    }
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
void x86_call_host(CPU *C, void *fn, const char *what)
{
    uint32_t eax, after, gsp = C->esp + 4;   /* +4: drop our fake return addr */
    if (!fn) { fprintf(stderr, "x86_call_host: %s unresolved\\n", what); abort(); }
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
    return bad;
}
'''

CMDS = {"report": cmd_report, "emit": cmd_emit, "runtime": cmd_runtime,
        "dll": cmd_dll}

if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in CMDS:
        sys.exit(__doc__)
    CMDS[sys.argv[1]](sys.argv[2:])
