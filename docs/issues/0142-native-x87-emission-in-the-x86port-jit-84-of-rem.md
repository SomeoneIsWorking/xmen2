---
id: 142
title: Native x87 emission in the x86port JIT (84% of remaining helper routing)
status: open
symptom: in-game jit routes ~20,700 x87 instructions/boot through the interpreter helper, one insn per helper call; x87 is the sole remaining bulk after MOVZX/MOVSX landed
tags: x86port,jit,codegen,x87,perf,issue-141
created: 2026-09-04
updated: 2026-09-04
---

## Context

Follow-up to #141. The helper-routing histogram (x86port `8ad6c9f`, C286) shows x87 is ~84% of instructions still translated to interpreter-helper calls in-game (20,701 of ~24,600). Every x87 op in a block is a helper call embedding the decoded X86pInsn and invoking `x86p_execute_decoded` -- no native codegen, and each one also ends the straight-line run unless whitelisted.

The flat in-game profile (see memory: in-game-jit-perf-is-a-flat-profile) means this is the single largest broad codegen lever left. Native override of individual x87 leaves (`_ftol2` done) only picks off the localized hot ones.

## Why it's hard / deferred to its own issue

x87 native emission is large and correctness-sensitive:
- 8-deep register stack (ST0..ST7) with TOP field in the status word; push/pop wraparound
- control word: rounding mode + precision control (single/double/extended) actually affects results; XMLII sets it
- status word C0-C3 condition codes, consumed by FNSTSW AX -> SAHF -> JCC branch idioms
- 80-bit extended precision internally vs 64-bit host SSE2
- FPU tag word, exception masking
- `flags.c`/S043 is the EFLAGS authority; an x87 flag authority (status word) would be its analogue and needs the same silicon-verified treatment

## Approach sketch

- Map the x87 stack to 8 host xmm slots + a software TOP, or lean on host x87 (`-mfpu`) for exact 80-bit semantics
- Emit the common subset first (FLD/FST/FSTP/FADD/FMUL/FSUB/FDIV/FILD/FISTP/FCOM/FNSTSW/FLDCW/FSTCW), keep the rest on the helper
- Gate every step with jit.verify (already compares full CPU state incl. x87 registers on BlockEnd)
- Precision control: honour the guest control word or prove XMLII only ever uses one mode

## Not started. Needs a dedicated session.

## Progress (2026-09-04) -- perf confirmed the size; phase 1 (native FLD) landed

**Profiling settled the magnitude.** `perf record` over a 600-frame
act0/tutorial run (engine=jit): x87 is ~35-40% of CPU --
`x86p_x87_arith` 11.3%, `x86p_x87_execute` 8.3%, `x86p_x87_push` 3.4%,
`x86p_x87_from_f32` 2.7%, `read_float` 1.5%, `x86p_x87_get`/`to_f32`
~1% each, plus a large share of `x86p_execute_decoded` 4.5%. This is THE
in-game CPU lever, above every localized native override left.

`perf annotate` on `x86p_x87_arith` and `x86p_x87_push`: the cost is
**long dependency chains of x87 ops plus constant `long double` spills
to/from the stack** across the C call boundaries (`x86p_x87_get` returns
by pointer, helpers take `long double` by memory, `classify()` re-spills
to inspect the exponent). `fldcw` is NOT the bottleneck on this microarch
(<0.1% in the annotate). The fix is native emission that keeps values on
the host x87 stack within a block and shortens the chains.

**Phase 1 landed (x86port `6d1523b`, C287): native FLD.** FLD is the one
x87 instruction with no rounding and no reverse-operand trap -- float
widening to 80 bits is exact for every value -- so `fld dword`/`fld qword`
matches `x86p_x87_from_f32/f64` bit-for-bit. `emit_x87_load` widens inline
and calls `x86p_x87_push`/`x86p_x87_get` directly (DRY: the interpreter's
own functions own overflow flags / tags / TOP). Infra added and reusable
for the rest: `x86p_emit_alu_r64_imm8` (stack scratch), `x86p_emit_x87_m`
/ `x86p_emit_x87_reg`, the `sub rsp,16 / ... / add rsp,16` scratch-slot
pattern with the bounds check kept before the sub. 246M jit.verify block
entries agree, 0 divergence. Measured: -1.0% `x87_execute`, -1.0%
`from_f32`, -0.5% `read_float`, -0.6% `execute_decoded`; net ~2-3% CPU
(FLD is the cheapest x87 family -- no arith).

## Phase 2+ design notes (for the next session)

**FST/FSTP** (21k, 23% of x87): ~~the trap is that `x86p_x87_to_f32` does a
C `(float)v` cast, which rounds to nearest **ignoring the guest control
word**.~~ RESOLVED 2026-09-04: `x86p_x87_to_f32`/`to_f64` now round by the
guest CW's RC field -- on x86 they run `fldt`/`fldcw guest`/`fstps`/`fldcw
host` on the real unit (the `HOST_OP` pattern), elsewhere `fesetround`
around the cast. `test_fst_rounds_by_the_control_word` proves it with
hand-computed nearest-even / up / down / truncate anchors on a
half-ulp value plus a routing sweep vs the host FPU. Register forms
(FST/FSTP ST(i)) are native as of phase 2. Native FST/FSTP-**to-memory**
is now unblocked but deferred: the interpreter checks ST(0) emptiness
*before* touching memory, so a native path must get ST(0) before the
bounds check to match on the (pathological) empty-ST(0) + OOB-address
case -- an ordering wrinkle worth its own focused change.

**FADD/FSUB/FMUL/FDIV** (18k+, the `x86p_x87_arith` 11.3%): emit `fldt`
both operands from `reg[phys]`, the host op, `fstpt` back, with the guest
CW loaded (`fnstcw`/`fldcw`/`fldcw` per op like `HOST_OP`, or once per
block if a frame is added). The reverse-operand forms (FSUBR/FDIVR and
the `DC`/`DE` register encodings) must be gotten right by construction --
load operands in the order that makes non-reverse a plain `fsub %st(1),%st`.
Status word: replicate `x86p_x87_arith`'s `ZE` on div-by-zero and
`IE|SF` on an empty dst -- `jit.verify` compares SW, so any miss fails
the gate on the first NaN/zero. `classify()` for the result tag: match
its exact Zero/Special/Valid rule (denormals are Valid), FNSAVE's tag
word makes the distinction observable.

**`classify()` itself** is a cheap independent win for the
interpreter+helper path: rewrite it to read the stored 80-bit exponent
field instead of `fldz`/`fucompi` + re-spill. ~1% and near-zero risk.

## Progress (2026-09-04) -- phase 2 landed: native FADD/FSUB/FMUL/FDIV + FST/FSTP ST(i)

**`emit_x87_arith`** covers `FADD/FSUB/FMUL/FDIV` and their R and P
variants in the three operand shapes `arith_operands` (x87_exec.c)
recognises. It widens the source to 80 bits into a 16-byte stack slot
(host `fld` from memory, `x86p_x87_get` from a stack register) and calls
**`x86p_x87_arith` directly** -- the interpreter's own authority -- so the
reverse flag, the ZE/IE/SF status bits, the divide-by-zero infinity and
the result tag are correct by construction, not reimplemented in
assembly. System V passes the `long double` src in memory, so the slot IS
the outgoing argument. `do_pops` is inlined; an empty source register
skips straight to stack cleanup, matching `arith_operands` returning 0.
The FI forms (`x87_mem_int`) stay on the helper.

**`emit_x87_store_reg`** covers `FST/FSTP ST(i)` -- both slots are 80-bit,
no rounding, so it is just `get` ST(0) + `set` ST(i) + optional pop.
FST/FSTP to *memory* stays on the helper: the `to_f32`/`to_f64` RC
question above is unresolved and is the next step.

**`classify()` was already cheap** -- it is `v == 0.0L` + `isnan/isinf`,
no `fldz`/`fucompi` and no exponent re-spill. The design note above was a
misread; there is no interpreter win to take there.

**Structure debt.** `jit_x64.c` is 2,158 lines -- past the 2,000-line
extraction threshold. The x87 emission family (predicates + `emit_x87_load`
/`emit_x87_arith`/`emit_x87_store_reg` + the `x87_*` helpers, ~250 lines)
is a cohesive unit that should move to its own translation unit, which
needs a small `jit_x64` internal header exposing `BlockCtx`,
`emit_mem_prepare_w`, and the register constants. Do this before phase 3
adds compares/FILD/FISTP/mem-store emission.

**Gates.** jit.verify 248,040,303 in-game block entries agree, 0
divergence (act0/tutorial, 600 frames, engine=jit). x86port 19/19
(`test_jit_x64` widened with FSUB/FDIVR mem, FADD ST(0),ST(i), FDIVRP,
FST ST(i), FSUBR ST(i),ST(0) differential cases). Helper x87 routing
14,930 -> 8,499 translations/boot; the remainder is compares, FILD/FISTP,
FST/FSTP-to-memory, FLDCW/FNSTSW.
