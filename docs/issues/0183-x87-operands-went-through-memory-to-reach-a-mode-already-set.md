---
id: 183
title: every x87 operation moved both operands through memory to reach a mode the host unit was already in
status: resolved
symptom: x86p_x87_arith_raw was 15.2% of a Dead Zone gameplay run and its two hottest instructions were the loads after its own stores
state_items: S002,S010
tags: x87,performance,x86port,jit
created: 2026-09-22
updated: 2026-09-22
---

# 0183 — x87 operands went through memory to reach a mode already set

State items: S002 (JIT gameplay execution), S010 (performance)

Shared owner: `shared/x86port`, landed at `1c30243`; this port pins it in
`bootstrap.py`.

## What it was

On a host with a real x87 unit, `x86p_x87_arith_raw` performs the guest's
operation on that unit under the guest's control word — which is the
semantics, not a shortcut, because x87 rounds once at the selected precision
and computing in extended and re-rounding disagrees on 1.18% of operations.

The sequence bracketed the instruction with `fnstcw` / `fldcw` … `fldcw`, and
named its operands with `"m"` constraints so it could. Both operands therefore
went out as ten-byte `fstpt` stores and came straight back as `fldt` loads. A
ten-byte access cannot be forwarded from the store buffer, so each pair stalls
about fifteen cycles, and a single guest FMUL paid that several times over:
once for the caller's argument, twice for the operands, once for the result,
and once more for the classify that reads the result's exponent back out of
the register file it was just stored into.

Measured, `perf record` over the `deadzone-render` case:

```
15.20%  x86p_x87_arith_raw
```

and inside it the two hottest instructions, at 10.77% and 7.95% of the
function, were the instructions immediately after those stores.

The first hypothesis was that the FLDCW pair itself was the cost. **It is
not**: 20,000,000 multiplies through the sequence cost 12.40 ns each, and the
same sequence with the two FLDCWs skipped when the host word already matched
cost 12.25 ns. That arm changed nothing because the operands still went
through memory.

## What it is now

When the host's control word already equals the guest's, loading it and
restoring it are both no-ops — so the operation is exactly what the C operator
compiles to, with its operands left in x87 registers. This has **no**
precondition on the values: same rounding mode, same precision, same six
exception masks, therefore the same result and the same raised flags. When the
words differ the old sequence runs unchanged, at its old cost.

That is worth testing for because a guest picks a mode and keeps it. The
census now reports the mode on every host, and on this route X-Men Legends II
runs **all 98,792,279 operations at 64-bit extended precision** and 98.3% of
them at round-to-nearest — which is this host's own default state.

| | before | after |
|---|---|---|
| one multiply, 20,000,000 of them | 12.4 ns | 4.8 ns |
| `x86p_x87_arith_raw`, `deadzone-render` | 15.20% | 4.64% |
| all x87 symbols, same run | ~28% | ~16.5% |
| cycles for the whole case | 56.3 G | 50.9 G |

## How it is held

`tests/test_x87.c::test_the_control_word_fast_arm_is_the_same_operation` runs
both arms over the same 3,145,728 operand pairs — putting the host unit into
the swept control word for one side and leaving it alone for the other — and
compares the result bit for bit **and** the status word. 2,621,440 of the
pairs genuinely take a different arm on each side, which the test prints and
asserts is non-zero: without that, every pair would be the same arm run twice
and would agree for a reason the test is not about.

Against a fast arm deliberately off by one ulp it fails on 263,140 pairs.

## What this does not cover

The arithmetic. What is left is the plumbing around it: a guest `FMUL ST,ST(i)`
still emits three separate indirect calls (`x86p_x87_get`, `jit_x87_arith`,
`x86p_x87_pop`) with a 16-byte stack slot opened around them, and
`x86p_jit_engine_run` is now the largest single symbol at 21.8%. Folding the
common register-operand shapes into one call is the next lever and is not done.

The JIT also still reports `0 of 0 x87 load(s) widened` and `0 of 0 x87
store(s) narrowed` — the x86-64 backend does not count guest x87 memory
operations as such, so those denominators say the lever is absent rather than
unused.
