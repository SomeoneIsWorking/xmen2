# 0167 — the browser emulates SSE lane by lane in C, on a target that has SIMD

- **State items:** S021
- **Status:** measured, not started
- **Found by:** reading the profile after #162's pop fusion, when
  `x86p_wasm_simd_arithmetic` was the fifth-largest frame in the guest worker

## The number

Guest worker, Dead Zone route, 25s, 96,169 samples:

| frame | share |
|---|---|
| `x86p_wasm_simd_arithmetic` | 3.23% |

Its callees are inlined into it by the release build (`8b18aab`), so that self
time is the whole cluster: the crossing, the operand marshalling and the lane
work.

## What it does

`src/x86port/jit_wasm_simd.c:276` emits, for every SSE arithmetic instruction,
a call to the `wasm_simd_arithmetic` import. The helper copies the destination
register out of `cpu->xmm`, reassembles the second operand from four separate
i32 arguments, and hands both to `x86p_simd_int` / `x86p_simd_float`, which do
the work **one lane at a time in scalar C**.

The import takes eight parameters and returns one. Eight is the wasm import
ABI's hard arity cap here -- `write_types` declares an i32 return for one
through eight parameters -- so the operand cannot be widened and a ninth
argument is not available. That is a fact about extending this design, not a
reason to extend it.

## Why this is structural

**WebAssembly has a 128-bit SIMD instruction set and `emit_wasm` cannot emit a
single instruction from it.** There is no `v128` opcode, no `f32x4`, no `0xFD`
prefix anywhere in `src/x86port/emit_wasm.{h,c}`. The backend is not choosing
a helper over an inline lowering; the inline lowering does not exist as a
capability.

So this is the same shape as the x87 pop in #162 and the dispatch crossings in
#166 -- work that crosses the import boundary because the emitter has no way to
express it -- except that here the host instruction exists and maps almost one
to one: `addps` to `f32x4.add`, `mulps` to `f32x4.mul`, `paddd` to `i32x4.add`.

## The gate to measure before writing any of it

The x87 equivalent of this question killed a whole plan in #162, so ask it
first and answer it by counting operations rather than by reasoning:

1. **What rounding mode does the guest's MXCSR hold, over operations rather
   than over `LDMXCSR` events?** SSE honours MXCSR's rounding-control field;
   wasm's `f32x4` arithmetic is round-to-nearest-even only. An inline lowering
   is valid only for the modes wasm can produce, and if the guest uses another
   mode at all the lowering needs a guarded fallback to the helper rather than
   a silent difference.
2. **Are flush-to-zero or denormals-are-zero ever set?** Those change results
   for subnormal inputs and wasm has no equivalent control.
3. **Which operations actually run?** A histogram by `X86pSimdOp` says which
   handful of lowerings buy the 3.23% and which are rare enough to leave on the
   helper. Do not implement the enum.

The instrument must be able to show the other answer, as #162's did when
rounding control moved on 1.65% of x87 operations. A counter that reports
"nearest, always" without ever having been able to report anything else proves
nothing.

## What this is worth, and what it is not

3.23% is the ceiling of the arithmetic cluster and it is not the whole prize:
`comiss`/`ucomiss` write `cpu->flags` through the same crossing, and operand
movement and loads would also gain from a `v128` capability once it exists.
Equally, nothing here has been built, so 3.23% is a ceiling and not a promise.

It is smaller than dispatch (#166, 13.1% with the intercept) and smaller than
what is left of x87 (#162, about 33%). It is listed here so the ranking is
made on measured numbers rather than on which file was open.
