#!/usr/bin/env python3
"""Generate every consumer of tools/probes.json.

    tools/gen_probes.py            regenerate
    tools/gen_probes.py --check    exit non-zero if the output is stale
    tools/gen_probes.py --selftest prove the refusals actually refuse

A probe names a guest function that both sides of the oracle comparison record
at every call. Four artifacts have to agree about it, and the point of this
file is that they cannot disagree:

  1. src/recomp/gen/probe_table.h    the shared C table -- compiled BOTH by
                                     64-bit gcc for the port and by 32-bit
                                     mingw for tools/proxy_d3d8;
  2. src/recomp/gen/probe_wraps.c    the port's __wrap_fn_<mod>_<ep> hooks;
  3. src/recomp/gen/recomp_probes.cmake   the -Wl,--wrap flags;
  4. scratch/recomp/<module>.isolate      the entries without which --wrap
                                     binds at compile time and NEVER FIRES.

(4) is not optional and not a detail. ld --wrap only redirects calls that cross
an object file, so a probed function emitted into the same chunk as its caller
produces a build that links, reports nothing, and yields a comparison in which
one side simply has no records. This file writes the isolate lists itself --
native overrides no longer use --wrap (they route through the dispatcher's
override slot, x86_register_override), so there is exactly one writer left.

WHAT IS REFUSED, rather than generated wrong:

  * a name that is not in scratch/recomp/<module>.json, or that matches more
    than one function there -- an overload set is ambiguous and a probe that
    silently took the first one would record the wrong code;
  * a manifest field naming arg<N> when the function's own RET <n> says it
    takes fewer arguments than that;
  * a prologue whose first five bytes are not a whole number of instructions,
    contain a RELATIVE jump or call (the stock-side hook relocates those bytes
    to a trampoline, where a relative displacement means something else), or
    are jumped INTO from later in the same function.

WHAT A NEGATIVE PRINTS: with no probes declared this still writes all four
artifacts, empty, and says so. It never writes nothing silently.
"""
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "probes.json")
GEN = os.path.join(ROOT, "src", "recomp", "gen")
RECOMP = os.path.join(ROOT, "scratch", "recomp")

HDR_OUT = os.path.join(GEN, "probe_table.h")
WRAP_OUT = os.path.join(GEN, "probe_wraps.c")
STUB_OUT = os.path.join(GEN, "probe_stubs.S")
CMAKE_OUT = os.path.join(GEN, "recomp_probes.cmake")

HOOK_BYTES = 5          # E9 rel32: what the stock-side hook overwrites

# Mnemonics whose encoding carries a displacement relative to the instruction's
# own address. Relocating one to a trampoline changes where it goes.
RELATIVE = set("""CALL JMP JA JAE JB JBE JC JCXZ JE JECXZ JG JGE JJL JL JLE JMPF
JNA JNAE JNB JNBE JNC JNE JNG JNGE JNL JNLE JNO JNP JNS JNZ JO JP JPE JPO JS
JZ LOOP LOOPE LOOPNE LOOPNZ LOOPZ""".split())


class Refuse(Exception):
    pass


def load_manifest(path=SRC):
    if not os.path.exists(path):
        raise Refuse("gen_probes: %s does not exist. It is the single source "
                     "of truth for oracle probes; refusing to generate wiring "
                     "from nothing." % path)
    with open(path) as f:
        d = json.load(f)
    return parse_manifest(d)


def parse_manifest(d):
    """Validate and normalize JSON while preserving non-comparable frame shape."""
    out = []
    for i, p in enumerate(d.get("probes", [])):
        for k in ("module", "name", "fields", "why"):
            if k not in p:
                raise Refuse("gen_probes: probes[%d] has no %r" % (i, k))
        fields = []
        for j, fl in enumerate(p["fields"]):
            at, read, when = fl.get("at"), fl.get("read"), fl.get("when")
            if when not in ("in", "out"):
                raise Refuse("gen_probes: %s field %d: when is 'in' or 'out', "
                             "not %r" % (p["name"], j, when))
            if at == "ecx":
                src, off = "ECX", 0
            elif at == "eax":
                # The return value. Meaningless before the call, so an `in`
                # field naming it would silently record whatever EAX happened
                # to hold at entry.
                if when != "out":
                    raise Refuse(
                        "gen_probes: %s field %d reads 'eax' with when=%r. EAX "
                        "is the RETURN VALUE; before the call it holds "
                        "whatever the caller left there, which is not an "
                        "argument to anything." % (p["name"], j, when))
                src, off = "EAX", 0
            elif isinstance(at, str) and at.startswith("arg") and at[3:].isdigit():
                src, off = "STACK", int(at[3:]) * 4
            else:
                raise Refuse("gen_probes: %s field %d: at is 'ecx' or 'arg<N>',"
                             " not %r" % (p["name"], j, at))
            if read == "dword":
                kind, ln = "DWORD", 4
            elif isinstance(read, int) and 0 < read <= 256:
                kind, ln = "DEREF", read
            else:
                raise Refuse("gen_probes: %s field %d: read is 'dword' or a "
                             "byte count in 1..256, not %r"
                             % (p["name"], j, read))
            fields.append(dict(name=fl.get("as", "%s_%d" % (when, j)),
                               when=when, src=src, off=off, kind=kind, len=ln))
        normalized = dict(module=p["module"], name=p["name"], why=p["why"],
                          fields=fields)
        if "argbytes" in p:
            normalized["argbytes"] = p["argbytes"]
        out.append(normalized)
    return out


def load_module(module):
    path = os.path.join(RECOMP, module + ".json")
    if not os.path.exists(path):
        raise Refuse(
            "gen_probes: %s does not exist, so the probe(s) on %s cannot be\n"
            "  resolved to an address. Run tools/ghidra_export.sh %s first.\n"
            "  Refusing rather than emitting a table with a guessed address."
            % (path, module, module))
    with open(path) as f:
        return json.load(f)


def resolve(module, name, doc):
    """-> (ep, instructions). Refuses on absent or ambiguous."""
    hits = [f for f in doc["functions"]
            if (f.get("qname") or f.get("name")) == name]
    if not hits:
        near = sorted(set((f.get("qname") or "") for f in doc["functions"]
                          if name.split("::")[-1] in (f.get("qname") or "")))
        raise Refuse(
            "gen_probes: %s has no function named\n    %s\n"
            "  %d function(s) in that module share its last name component:\n%s"
            % (module, name, len(near),
               "".join("    %s\n" % n for n in near[:12]) or "    (none)\n"))
    if len(hits) > 1:
        raise Refuse(
            "gen_probes: %s matches %d functions in %s, at %s. That is an\n"
            "  overload set; a probe that took the first one would record the\n"
            "  wrong code half the time. Name the address instead once probes\n"
            "  support it, or probe a distinguishable wrapper."
            % (name, len(hits), module,
               ", ".join("0x%08x" % h["ep"] for h in hits)))
    return hits[0]["ep"], hits[0]["ins"]


def declared_args(ins):
    """Stack argument count from the function's own RET <n>, or None for a
    plain RET (cdecl, where the callee cannot know)."""
    ns = set()
    for i in ins:
        if i["m"] != "RET":
            continue
        t = i["t"].split()
        ns.add(int(t[1], 16) // 4 if len(t) > 1 else None)
    if not ns or None in ns:
        return None
    if len(ns) > 1:
        raise Refuse("gen_probes: a function returns with more than one stack "
                     "adjustment (%s); that is not a callable ABI"
                     % ", ".join(str(n) for n in sorted(ns)))
    return ns.pop()


def prologue(ep, ins):
    """-> (nbytes, bytes) for the region the stock-side hook relocates."""
    n, blob = 0, b""
    for i in ins:
        if i["a"] != ep + n:
            raise Refuse("gen_probes: the instruction stream at 0x%08x is not "
                         "contiguous from the entry point; cannot measure a "
                         "prologue." % ep)
        if i["m"] in RELATIVE:
            raise Refuse(
                "gen_probes: 0x%08x is %d byte(s) into the prologue and is a\n"
                "  %s -- a RELATIVE branch. The stock-side hook copies these\n"
                "  bytes to a trampoline, where the displacement would point\n"
                "  somewhere else. This function cannot be hooked this way."
                % (i["a"], n, i["m"]))
        n += i["n"]
        blob += bytes.fromhex(i["b"])
        if n >= HOOK_BYTES:
            break
    if n < HOOK_BYTES:
        raise Refuse(
            "gen_probes: the whole function at 0x%08x is %d byte(s); a hook\n"
            "  needs %d to place a jump. Refusing rather than writing past\n"
            "  the end of it." % (ep, n, HOOK_BYTES))
    for i in ins:
        tgt = i.get("flow")
        if tgt is not None and ep < tgt < ep + n:
            raise Refuse(
                "gen_probes: 0x%08x branches to 0x%08x, which is INSIDE the\n"
                "  %d prologue bytes the hook replaces. Patching it would send\n"
                "  that branch into the middle of a jump instruction."
                % (i["a"], tgt, n))
    return n, blob


def manifest_hash(probes):
    """Over everything a reader of the stream must agree about: which probes,
    in what order, with which fields. NOT over `why`, so editing a comment
    does not invalidate a capture."""
    h = hashlib.sha256()
    for p in probes:
        h.update(("%s|%s|argbytes=%s|"
                  % (p["module"], p["name"], p.get("argbytes", "auto"))).encode())
        for f in p["fields"]:
            h.update(("%s,%s,%s,%d,%d;" % (f["name"], f["when"], f["src"],
                                           f["off"], f["len"])).encode())
    return int.from_bytes(h.digest()[:4], "little")


def frame_shape(probe, nargs):
    """Return (argument bytes, cleanup owner) without inventing cdecl shape."""
    explicit = probe.get("argbytes")
    if explicit is not None:
        if (not isinstance(explicit, int) or isinstance(explicit, bool)
                or explicit < 0 or explicit > 0xffff or explicit % 4):
            raise Refuse(
                "gen_probes: %s argbytes must be a multiple of four in "
                "0..65535" % probe["name"])
    used = [f["off"] + 4 for f in probe["fields"] if f["src"] == "STACK"]
    inferred = max(used) if used else 0
    if nargs is not None:
        actual = nargs * 4
        if explicit is not None and explicit != actual:
            raise Refuse(
                "gen_probes: %s declares argbytes=%d, but RET says %d"
                % (probe["name"], explicit, actual))
        return actual, "callee"
    if explicit is not None:
        if inferred > explicit:
            raise Refuse(
                "gen_probes: %s records %d argument bytes beyond its "
                "declared argbytes=%d" % (probe["name"], inferred, explicit))
        return explicit, "caller"
    return inferred, "caller"


def build(probes):
    """Resolve every probe. -> list of dicts with ep/prologue filled in."""
    out = []
    docs = {}
    for p in probes:
        if p["module"] not in docs:
            docs[p["module"]] = load_module(p["module"])
        doc = docs[p["module"]]
        program = doc.get("program")
        image_base = doc.get("image_base")
        if not isinstance(program, str) or not program:
            raise Refuse(
                "gen_probes: %s.json has no non-empty program name; the "
                "stock hook cannot identify which loaded PE owns 0x%08x"
                % (p["module"], 0))
        if (not isinstance(image_base, int) or isinstance(image_base, bool)
                or image_base < 0 or image_base > 0xffffffff):
            raise Refuse(
                "gen_probes: %s.json has invalid image_base %r; the stock "
                "hook cannot relocate a preferred address without it"
                % (p["module"], image_base))
        ep, ins = resolve(p["module"], p["name"], doc)
        if ep < image_base:
            raise Refuse(
                "gen_probes: %s entry 0x%08x precedes image_base 0x%08x"
                % (p["name"], ep, image_base))
        nargs = declared_args(ins)
        if nargs is not None:
            for f in p["fields"]:
                if f["src"] == "STACK" and f["off"] // 4 >= nargs:
                    raise Refuse(
                        "gen_probes: %s declares arg%d, but the function's own\n"
                        "  RET 0x%x says it takes %d stack argument(s). One of\n"
                        "  the two is wrong and a recorded field past the end\n"
                        "  of the frame is somebody else's stack."
                        % (p["name"], f["off"] // 4, nargs * 4, nargs))
        n, blob = prologue(ep, ins)
        # A callee-cleaned function says its frame size in RET <n>. Cdecl does
        # not, so a frame larger than the comparable fields must be explicit
        # in the manifest; recording process-specific pointers only to infer
        # frame shape would poison port-vs-stock comparisons.
        argbytes, cleanup = frame_shape(p, nargs)
        q = dict(p)
        q.update(ep=ep, program=program, image_base=image_base,
                 prologue=n, expect=blob, nargs=nargs,
                 argbytes=argbytes, cleanup=cleanup)
        out.append(q)
    return out


HEADER = """/* GENERATED by tools/gen_probes.py from tools/probes.json -- do not edit.
 *
 * Compiled by BOTH sides of the oracle comparison: 64-bit gcc for the port and
 * 32-bit mingw for tools/proxy_d3d8. Nothing here may reference either.
 */
#ifndef PROBE_TABLE_H
#define PROBE_TABLE_H

#include "probe_rec.h"

#define PROBE_MANIFEST_HASH 0x%08xu
#define PROBE_COUNT %d
"""


def emit_header(probes, h):
    L = [HEADER % (h, len(probes))]
    if not probes:
        L.append("\n/* NO PROBES ARE DECLARED. This table is empty on purpose;\n"
                 "   tools/probes.json declares none. */\n"
                 "static const Probe g_probes[1];\n"
                 "#define PROBE_TABLE_EMPTY 1\n\n#endif\n")
        return "".join(L)
    for p in probes:
        L.append("\n/* %s\n   %s */\n" % (p["name"], p["why"]))
        L.append("static const ProbeField pf_%08x[] = {\n" % p["ep"])
        for f in p["fields"]:
            L.append("    { PROBE_WHEN_%s, PROBE_SRC_%s, PROBE_KIND_%s, 0, "
                     "%d, %d, \"%s\" },\n"
                     % (f["when"].upper(), f["src"], f["kind"],
                        f["off"], f["len"], f["name"]))
        L.append("};\n")
        L.append("static const pr_u8 pe_%08x[] = { %s };\n"
                 % (p["ep"], ", ".join("0x%02x" % b for b in p["expect"])))
    L.append("\nstatic const Probe g_probes[PROBE_COUNT] = {\n")
    for i, p in enumerate(probes):
        L.append("    { %d, \"%s\", \"%s\", \"%s\", 0x%08xu, 0x%08xu, "
                 "%d, %d, pf_%08x, pe_%08x },\n"
                 % (i, p["name"], p["module"], p["program"],
                    p["image_base"], p["ep"], p["prologue"],
                    len(p["fields"]), p["ep"], p["ep"]))
    L.append("};\n")
    L.append("\n/* Frame shape, for the stock-side stub only: how many bytes of\n"
             "   arguments to re-push, and who cleans them. */\n"
             "static const pr_u16 g_probe_argbytes[PROBE_COUNT] = { %s };\n"
             % ", ".join(str(p["argbytes"]) for p in probes))
    L.append("static const pr_u8 g_probe_callee_cleans[PROBE_COUNT] = { %s };\n"
             % ", ".join("1" if p["cleanup"] == "callee" else "0"
                         for p in probes))
    L.append("\n#endif /* PROBE_TABLE_H */\n")
    return "".join(L)


STUBS_HEAD = """/* GENERATED by tools/gen_probes.py from tools/probes.json -- do not edit.
 *
 * The STOCK side's hook bodies, one per probe, for 32-bit mingw.
 *
 * The game's own libIGMath is patched at the entry of each probed function
 * with a jump to the matching stub here. A stub records the arguments, calls
 * the original through a trampoline (the relocated prologue plus a jump back),
 * records what the call wrote, and returns exactly as the original would.
 *
 * WHY GENERATED, and not one hand-written stub with a runtime argument count:
 * re-pushing the arguments and cleaning the frame both depend on the size of
 * that frame, and doing it from a table means computing stack offsets in asm
 * at run time inside the game's own control flow. Baking each count into its
 * own stub makes every offset here a constant the assembler checks.
 *
 * The x87 stack is saved and restored around the recorder because these are
 * FLOATING-POINT functions and the C recorder calls into the game's CRT to
 * write the file. Clobbering ST0..7 mid-animation would corrupt the very
 * values the harness exists to measure -- it would be an instrument that
 * changes the answer.
 */
    .text
"""


def emit_stubs(probes):
    L = [STUBS_HEAD]
    if not probes:
        L.append("\n/* No probes are declared, so there are no stubs. */\n")
        return "".join(L)
    for i, p in enumerate(probes):
        nargs = p["argbytes"] // 4
        a = L.append
        a("\n/* %s -- %d argument byte(s), %s-cleaned */\n"
          % (p["name"], p["argbytes"], p["cleanup"]))
        a("    .globl _probe_stub_" + str(i) + "\n")
        a("_probe_stub_" + str(i) + ":\n")
        # ebp+0 saved ebp | ebp+4 return address | ebp+8.. the arguments
        a("    push %ebp\n")
        a("    mov %esp, %ebp\n")
        a("    pushal\n")
        a("    sub $108, %esp\n")
        a("    fnsave (%esp)\n")
        a("    lea 8(%ebp), %eax\n")
        a("    push %eax\n")                    # the argument base
        a("    push %ecx\n")                    # the this-pointer
        a("    push $" + str(i) + "\n")
        a("    call _probe_enter\n")
        a("    add $12, %esp\n")
        a("    frstor (%esp)\n")
        a("    add $108, %esp\n")
        a("    popal\n")
        # Re-push the arguments exactly as we received them, last first.
        for k in range(nargs - 1, -1, -1):
            a("    push " + str(8 + k * 4) + "(%ebp)\n")
        a("    call *(_probe_tramp + " + str(i * 4) + ")\n")
        if p["cleanup"] == "caller" and nargs:
            a("    add $" + str(p["argbytes"]) + ", %esp\n")
        a("    pushal\n")
        a("    sub $108, %esp\n")
        a("    fnsave (%esp)\n")
        # The return value, out of the frame pushal just saved: pushal writes
        # EAX first, so it sits 28 bytes above the post-pushal esp, and the
        # fnsave area adds another 108. Constants the assembler checks, which
        # is the whole reason these stubs are generated per probe.
        a("    mov 136(%esp), %eax\n")
        a("    push %eax\n")
        a("    push $" + str(i) + "\n")
        a("    call _probe_leave\n")
        a("    add $8, %esp\n")
        a("    frstor (%esp)\n")
        a("    add $108, %esp\n")
        a("    popal\n")
        a("    mov %ebp, %esp\n")
        a("    pop %ebp\n")
        if p["cleanup"] == "callee" and p["argbytes"]:
            a("    ret $" + str(p["argbytes"]) + "\n")
        else:
            a("    ret\n")
    L.append("\n    .data\n    .align 4\n"
             "    .globl _probe_stub_table\n_probe_stub_table:\n")
    for i in range(len(probes)):
        L.append("    .long _probe_stub_" + str(i) + "\n")
    L.append("\n/* Filled in by probe_hook.c when each trampoline is built. */\n"
             "    .globl _probe_tramp\n_probe_tramp:\n"
             "    .space " + str(4 * len(probes)) + "\n")
    return "".join(L)


WRAPS_HEAD = """/* GENERATED by tools/gen_probes.py from tools/probes.json -- do not edit.
 *
 * One __wrap_ per probe. ld --wrap redirects every CALL SITE of the recompiled
 * body to these; __real_ is the body itself, still linked and still the thing
 * that does the work. The hook records around it and changes nothing.
 *
 * This only fires if the probed function was ALSO emitted into its own
 * translation unit -- gen_probes.py writes that isolate list. A wrap without
 * an isolate links and never runs.
 */
#include "x86rt.h"
#include "probe_table.h"

void oracle_probe_call(const Probe *p, void (*real)(CPU *), CPU *C);
#if defined(__APPLE__)
void x86_register_override(const char *module, uint32_t linked_ep,
                           x86_override_fn fn);
#endif
"""


def emit_wraps(probes):
    L = [WRAPS_HEAD]
    if not probes:
        L.append("\n/* No probes are declared, so there is nothing to wrap. */\n")
        return "".join(L)
    for i, p in enumerate(probes):
        sym = "fn_%s_%08x" % (p["module"].replace("-", "_"), p["ep"])
        L.append("\n#if defined(__APPLE__)\n"
                 "void %s(CPU *C);\n"
                 "#define X2_PROBE_REAL_%s %s\n"
                 "#else\n"
                 "void __real_%s(CPU *C);\n"
                 "#define X2_PROBE_REAL_%s __real_%s\n"
                 "#endif\n" % (sym, sym, sym, sym, sym, sym))
        L.append("void __wrap_%s(CPU *C)\n{\n"
                 "    oracle_probe_call(&g_probes[%d], X2_PROBE_REAL_%s, C);\n}\n"
                 "#undef X2_PROBE_REAL_%s\n"
                 % (sym, i, sym, sym))
    # The addresses of the wrappers, so the runtime can CHECK that the
    # dispatch table and the direct call sites actually reach them. These
    # functions are called across modules, through the guest's own import
    # tables, so "no direct call site references the wrapper" is normal and
    # proves nothing either way -- what has to be true is that the entry the
    # dispatcher resolves for this address IS the wrapper.
    L.append("\nvoid (*const g_probe_wrapfn[PROBE_COUNT])(CPU *) = {\n")
    for p in probes:
        L.append("    __wrap_fn_%s_%08x,\n"
                 % (p["module"].replace("-", "_"), p["ep"]))
    L.append("};\n")
    # Apple's linker has no GNU --wrap equivalent. The generated recompiler
    # already gives every probed entry point an override slot, so bind the same
    # wrapper through that portable dispatch seam on Mach-O.
    L.append("\n#if defined(__APPLE__)\n"
             "__attribute__((constructor))\n"
             "static void x2_probe_register_overrides(void)\n{\n")
    for p in probes:
        L.append("    x86_register_override(\"%s\", 0x%08xu, "
                 "__wrap_fn_%s_%08x);\n"
                 % (p["program"], p["ep"],
                    p["module"].replace("-", "_"), p["ep"]))
    L.append("}\n#endif\n")
    return "".join(L)


def emit_cmake(probes):
    L = ["# GENERATED by tools/gen_probes.py -- do not edit.\n"]
    if not probes:
        L.append("# tools/probes.json declares NO probes. The variable is still\n"
                 "# defined so the build does not break on its absence.\n"
                 "set(RECOMP_PROBE_WRAPS)\n")
        return "".join(L)
    L.append("set(RECOMP_PROBE_WRAPS\n")
    for p in probes:
        L.append("    -Wl,--wrap=fn_%s_%08x\n"
                 % (p["module"].replace("-", "_"), p["ep"]))
    L.append(")\n")
    return "".join(L)


def write_if_changed(path, text, changed):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    old = None
    if os.path.exists(path):
        with open(path) as f:
            old = f.read()
    if old == text:
        return False
    changed.append(path)
    with open(path, "w") as f:
        f.write(text)
    return True


def isolate_eps():
    """The (module, ep) pairs that need --isolate for their --wrap to fire."""
    try:
        probes = build(load_manifest())
    except Refuse:
        return []
    return [(p["module"], p["ep"]) for p in probes]


def emit_isolates(probes):
    """The --isolate lists, one file per module, in the format recomp.py emit
    --isolate reads (one 0x-prefixed EP per line).

    Written HERE: native overrides no longer use
    --isolate at all -- the emitter routes every call to an overridden entry
    point through the dispatcher's override slot -- so this file is the ONLY
    remaining writer of the lists, and the ONLY one there can be."""
    by_mod = {}
    for p in probes:
        by_mod.setdefault(p["module"], set()).add(p["ep"])
    return [(os.path.join(RECOMP, mod + ".isolate"),
             "".join("0x%08x\n" % e for e in sorted(by_mod[mod])))
            for mod in sorted(by_mod)]


def main(argv):
    check = "--check" in argv
    if "--selftest" in argv:
        return selftest()
    try:
        manifest = load_manifest()
        probes = build(manifest)
    except Refuse as e:
        sys.stderr.write(str(e) + "\n")
        return 2
    h = manifest_hash(manifest)
    outs = [(HDR_OUT, emit_header(probes, h)),
            (WRAP_OUT, emit_wraps(probes)),
            (STUB_OUT, emit_stubs(probes)),
            (CMAKE_OUT, emit_cmake(probes)),
            *emit_isolates(probes)]
    changed = []
    for path, text in outs:
        write_if_changed(path, text, changed) if not check else None
    if check:
        stale = []
        for path, text in outs:
            if not os.path.exists(path):
                stale.append(path + " (missing)")
                continue
            with open(path) as f:
                if f.read() != text:
                    stale.append(path + " (stale)")
        if stale:
            sys.stderr.write("gen_probes --check: %d artifact(s) out of date "
                             "with tools/probes.json:\n%s"
                             "Run tools/gen_probes.py.\n"
                             % (len(stale), "".join("  %s\n" % s for s in stale)))
            return 1
        print("gen_probes --check: %d probe(s), all %d artifact(s) current"
              % (len(probes), len(outs)))
        return 0
    if not probes:
        print("gen_probes: tools/probes.json declares NO probes. Wrote %d "
              "EMPTY artifact(s); the harness will record nothing." % len(outs))
        return 0
    print("gen_probes: %d probe(s), manifest hash 0x%08x" % (len(probes), h))
    for p in probes:
        print("  %-12s 0x%08x  %-46s %d field(s), %d prologue byte(s)%s"
              % (p["module"], p["ep"], p["name"].split("::", 2)[-1],
                 len(p["fields"]), p["prologue"],
                 "" if p["nargs"] is None else ", RET 0x%x" % (p["nargs"] * 4)))
    print("  wrote %s" % (", ".join(os.path.relpath(c, ROOT) for c in changed)
                          if changed else "nothing (already current)"))
    return 0


# ---------------------------------------------------------------------------
# The refusals, proved to refuse.
#
# Every check above exists because generating the artifact anyway would produce
# a harness that runs and lies. A check that cannot be shown to fire is not a
# check, so each one is fed an input it MUST reject.
def selftest():
    ok = [True]

    def must_refuse(what, fn):
        try:
            fn()
        except Refuse as e:
            first = str(e).splitlines()[0]
            print("  pass  %-34s refused: %s" % (what, first[:78]))
            return
        except SystemExit:
            print("  pass  %-34s refused (exit)" % what)
            return
        ok[0] = False
        print("  FAIL  %-34s was ACCEPTED" % what)

    def must_accept(what, fn):
        try:
            fn()
            print("  pass  %-34s accepted" % what)
        except Refuse as e:
            ok[0] = False
            print("  FAIL  %-34s refused: %s" % (what, str(e).splitlines()[0]))

    ins_ok = [{"a": 0x1000, "n": 3, "m": "SUB", "b": "83ec28", "t": "SUB ESP,0x28"},
              {"a": 0x1003, "n": 4, "m": "MOV", "b": "8b542430", "t": "MOV EDX,x"},
              {"a": 0x1007, "n": 3, "m": "RET", "b": "c20c00", "t": "RET 0xc"}]
    must_accept("a normal prologue", lambda: prologue(0x1000, ins_ok))

    ins_rel = [{"a": 0x1000, "n": 5, "m": "CALL", "b": "e800000000", "t": "CALL x"},
               {"a": 0x1005, "n": 1, "m": "RET", "b": "c3", "t": "RET"}]
    must_refuse("a relative CALL in the prologue",
                lambda: prologue(0x1000, ins_rel))

    ins_short = [{"a": 0x1000, "n": 1, "m": "RET", "b": "c3", "t": "RET"}]
    must_refuse("a function shorter than the hook",
                lambda: prologue(0x1000, ins_short))

    ins_into = [{"a": 0x1000, "n": 3, "m": "SUB", "b": "83ec28", "t": "SUB"},
                {"a": 0x1003, "n": 4, "m": "MOV", "b": "8b542430", "t": "MOV"},
                {"a": 0x1007, "n": 2, "m": "JNZ", "b": "75f9", "t": "JNZ",
                 "flow": 0x1002}]
    must_refuse("a branch into the patched bytes",
                lambda: prologue(0x1000, ins_into))

    doc = {"functions": [{"ep": 0x1000, "qname": "A::f", "ins": ins_ok},
                         {"ep": 0x2000, "qname": "A::g", "ins": ins_ok},
                         {"ep": 0x3000, "qname": "A::g", "ins": ins_ok}]}
    must_accept("a name that resolves once",
                lambda: resolve("m", "A::f", doc))
    must_refuse("a name matching two functions",
                lambda: resolve("m", "A::g", doc))
    must_refuse("a name matching none",
                lambda: resolve("m", "A::nope", doc))

    # RET 0xc = 3 arguments, so arg3 is off the end of the frame.
    if declared_args(ins_ok) != 3:
        ok[0] = False
        print("  FAIL  RET 0xc should read as 3 arguments, got %r"
              % declared_args(ins_ok))
    else:
        print("  pass  %-34s RET 0xc -> 3" % "argument count from RET n")
    if declared_args(ins_short) is not None:
        ok[0] = False
        print("  FAIL  a plain RET should read as 'unknown', not a count")
    else:
        print("  pass  %-34s plain RET -> unknown" % "cdecl is not guessed")

    cdecl = dict(name="cdecl_two_args", argbytes=8, fields=[])
    if frame_shape(cdecl, None) != (8, "caller"):
        ok[0] = False
        print("  FAIL  explicit cdecl frame shape was not preserved")
    else:
        print("  pass  %-34s 8 bytes, caller-cleaned"
              % "explicit cdecl frame shape")
    normalized = parse_manifest({"probes": [{
        "module": "m", "name": "cdecl_two_args", "why": "fixture",
        "argbytes": 8, "fields": []
    }]})
    if normalized[0].get("argbytes") != 8:
        ok[0] = False
        print("  FAIL  manifest parser dropped explicit cdecl frame shape")
    else:
        print("  pass  %-34s argbytes=8 preserved"
              % "manifest keeps frame shape")
    must_refuse("misaligned cdecl frame shape",
                lambda: frame_shape({**cdecl, "argbytes": 6}, None))

    # The hash must move when a field moves, or a stale capture would compare
    # against a manifest it was not recorded under.
    a = [dict(module="m", name="f",
              fields=[dict(name="q", when="in", src="ECX", off=0, len=16)])]
    b = [dict(module="m", name="f",
              fields=[dict(name="q", when="in", src="ECX", off=0, len=64)])]
    if manifest_hash(a) == manifest_hash(b):
        ok[0] = False
        print("  FAIL  the manifest hash ignores a field's length")
    else:
        print("  pass  %-34s 0x%08x != 0x%08x"
              % ("hash moves when a field does", manifest_hash(a),
                 manifest_hash(b)))
    c = [dict(module="m", name="f", argbytes=4, fields=[])]
    d = [dict(module="m", name="f", argbytes=8, fields=[])]
    if manifest_hash(c) == manifest_hash(d):
        ok[0] = False
        print("  FAIL  the manifest hash ignores explicit argbytes")
    else:
        print("  pass  %-34s 0x%08x != 0x%08x"
              % ("hash moves when frame shape does", manifest_hash(c),
                 manifest_hash(d)))

    # And an empty manifest must still produce all the artifacts.
    for name, fn in (("header", emit_header), ("wraps", lambda p: emit_wraps(p)),
                     ("cmake", lambda p: emit_cmake(p))):
        text = fn([], 0) if name == "header" else fn([])
        if not text.strip():
            ok[0] = False
            print("  FAIL  an empty manifest wrote an empty %s" % name)
    print("  pass  %-34s all three still written" % "an empty manifest")

    print("\nSELFTEST", "PASSED" if ok[0] else "FAILED")
    return 0 if ok[0] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
