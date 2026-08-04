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
            "undefined1": 1, "undefined2": 2, "undefined4": 4, "undefined8": 8}


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
            return "0x%xU" % (self.val & 0xFFFFFFFF)
        if self.kind == "mem":
            return "RD%d(%s)" % (self.size * 8, self.addr())
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

    m = re.match(r"^(byte|word|dword|qword|undefined\d*)\s+ptr\s+(.*)$", tok, re.I)
    if m:
        size = PTR_SIZE.get(m.group(1).lower())
        if size is None:
            raise Unsupported("ptr size %r" % m.group(1))
        return parse_mem(m.group(2), size)
    if tok.startswith("[") and tok.endswith("]"):
        o = parse_mem(tok, 4)
        o.inferred = True          # width not stated; caller must reconcile it
        return o

    # segment-relative -- FS: is used for SEH and we do not model it
    if re.match(r"^(FS|GS|CS|DS|ES|SS):", up):
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
        return Operand("mem", size=size, inferred=False,
                       addr_expr="0x%xU" % _num(tok))
    inner = m.group(1).strip()
    if re.match(r"^(FS|GS|CS|DS|ES|SS):", inner, re.I):
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
            term = "0x%xU" % (_num(p) & 0xFFFFFFFF)
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


def iat_symbol(addr_expr):
    """If a memory operand is a bare absolute address that is an IAT slot,
    return its (module, symbol); else None."""
    m = re.fullmatch(r"\(uint32_t\)\((0x[0-9a-f]+)U\)", addr_expr)
    if not m:
        m = re.fullmatch(r"(0x[0-9a-f]+)U", addr_expr)
    if not m:
        return None
    return IAT.get(int(m.group(1), 16))


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
            return [A, "fn_%08x(C); return;" % ins["flow"]]
        return [A, "goto L_%08x;" % ins["flow"]]

    if m.startswith("J") and m[1:] in CC:
        if "flow" not in ins:
            raise Unsupported("conditional jump with no resolved target")
        if ins["flow"] not in ctx["_addrs"]:
            return [A, "if (%s) { fn_%08x(C); return; }" % (CC[m[1:]], ins["flow"])]
        return [A, "if (%s) goto L_%08x;" % (CC[m[1:]], ins["flow"])]

    if m == "CALL":
        if ins.get("ind") or "flow" not in ins:
            raise Unsupported("indirect CALL")
        ret = ins["a"] + ins["n"]
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
    L = ['/* generated by tools/recomp.py runtime -- do not edit */',
         '#include "x86rt.h"', '#include <stdio.h>', '#include <stdlib.h>', '']
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
    for va in sorted(IAT):
        mod, sym = IAT[va]
        ident = c_ident(mod, sym)
        if ident in seen:
            continue
        seen.add(ident)
        L.append('void %s(CPU *C) { (void)C; x86_missing_import("%s", "%s"); }'
                 % (ident, mod, sym.replace('"', "'")))
    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    print("runtime: %d function-table entries, %d import stubs -> %s"
          % (len(fns), len(seen), out))


RUNTIME_BODY = '''
void x86_dispatch(CPU *C, uint32_t target)
{
    int i;
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].ep == target) { g_fns[i].fn(C); return; }
    fprintf(stderr, "x86_dispatch: no recompiled function at 0x%08x "
                    "(indirect call target outside the translated set)\\n", target);
    abort();
}

void x86_untranslated(uint32_t ep, const char *name, const char *reason)
{
    fprintf(stderr, "x86_untranslated: reached 0x%08x %s -- blocked by: %s\\n",
            ep, name, reason);
    abort();
}

void x86_missing_import(const char *mod, const char *sym)
{
    fprintf(stderr, "x86_missing_import: %s!%s is not implemented\\n", mod, sym);
    abort();
}
'''

CMDS = {"report": cmd_report, "emit": cmd_emit, "runtime": cmd_runtime}

if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in CMDS:
        sys.exit(__doc__)
    CMDS[sys.argv[1]](sys.argv[2:])
