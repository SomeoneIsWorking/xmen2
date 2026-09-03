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
