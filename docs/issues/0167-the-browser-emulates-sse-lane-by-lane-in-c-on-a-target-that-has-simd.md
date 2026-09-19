# 0167 — the browser emulates SSE lane by lane in C, on a target that has SIMD

- **State items:** S021
- **Status:** the gate below is answered and passed. 317,883,827 SSE arithmetic
  operations on the Dead Zone route run at round-to-nearest with flush-to-zero
  and denormals-are-zero both clear, 100.00% of them, and four opcodes account
  for every one of them. Every one of those four maps to a single WebAssembly
  SIMD instruction. The lowering is not written.
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

## The gate is answered: one rounding mode, four opcodes

A census was put in `x86p_wasm_simd_arithmetic` — one increment per operation,
the MXCSR bucket taken from `cpu->mxcsr` at the moment of the call — and the
port's heartbeat printed all sixteen buckets every five seconds. It was landed
to be measured (x86port `7bbabbb`) and removed once it had answered, so neither
repository carries it now. Dead Zone route, 165 s, ending at **317,883,827
operations**, about two million a second.

### Rounding: nearest, and nothing else, on all of them

| MXCSR bucket | operations | share |
|---|---|---|
| RC=nearest, FTZ clear, DAZ clear | 317,883,827 | 100.00% |
| each of the other fifteen | 0 | 0.00% |

The other fifteen are listed in the log by name with their zeros, not omitted.
`down`, `up`, `to-zero`, and every combination with flush-to-zero and
denormals-are-zero, are each explicitly zero on this route.

This is the opposite of what the same question returned for x87 in #162, where
the rounding field moved on 1.65% of operations and killed the precision-control
plan outright. Here the field never moves, and wasm's `f32x4` arithmetic —
round-to-nearest-even, no flush-to-zero — is exactly what the guest is asking
for.

### The instrument can land in the other fifteen buckets

100% in one bucket is worth nothing from a counter that has only ever produced
one answer, so the same shipping helper was driven with each of the sixteen
control words and the bucket it moved was checked:

```
ok   mxcsr=0x0000 -> nearest            count 1
ok   mxcsr=0x2000 -> down               count 1
ok   mxcsr=0x8040 -> nearest+FTZ+DAZ    count 1
...
PASSED: 16 control words, 0 failure(s)
```

All sixteen land where they should. The 100.00% is a reading, not a stuck
counter. The report's negative also fired on its own during the run: before the
route reached gameplay it printed *"no operation reached the helper at all, so
this run says nothing about MXCSR"*, which is a different sentence from a table
of zeros, and both were seen.

### Which operations run: four of them, and that is all of them

| operation | count | share |
|---|---|---|
| `MULPS` | 120,507,136 | 37.91% |
| `ADDPS` | 118,903,152 | 37.40% |
| `SHUFPS` | 66,463,936 | 20.91% |
| `XORPS` | 12,009,600 | 3.78% |
| `ORPS` | 3 | 0.00% |

Those five sum to **317,883,827**, which is the denominator exactly: no other
`X86pSimdOp` reached the helper on this route at all. `SUBPS`, `DIVPS`,
`SQRTPS`, the `*ss` scalar forms, the integer `p*` family, `CMPPS`, `COMISS`,
`CVT*` — every one of them is zero here.

That is a much smaller target than the enum suggested. Four lowerings, each one
host instruction:

| guest | host |
|---|---|
| `MULPS` | `f32x4.mul` |
| `ADDPS` | `f32x4.add` |
| `XORPS` / `ORPS` | `v128.xor` / `v128.or` |
| `SHUFPS xmm, xmm, imm8` | `i8x16.shuffle` with the sixteen lane indices baked from the immediate |

`SHUFPS`'s immediate is a decode-time constant, so its selector is emitted into
the shuffle rather than computed, and the fourth-largest cost in the table
becomes one instruction with no branch.

### What is still not true

- **The lowering does not exist.** `emit_wasm` still has no `v128` type, no
  `0xFD` prefix and no SIMD opcode. That capability is the work; the census only
  says it is worth building and which four instructions to build first.
- **The `xmm` operand path is the other half of the prize.** `read_source`
  currently emits four separate `i32.load`s per source and `store_lanes` four
  stores per destination, all against `cpu->xmm[reg]`. Those are contiguous
  16-byte fields (`xmm_offset` in `jit_wasm_state.c` is `offsetof(X86pCpu, xmm) +
  index * 16 + lane * 4`), so with a `v128` capability each becomes one
  `v128.load` / `v128.store`. The 3.23% is the helper's self time and does not
  include that lane traffic, so it is a floor for this change rather than the
  ceiling the earlier section called it.
- **Four opcodes on one route is not four opcodes on every route.** A guarded
  design keeps the helper for everything not lowered, and the mode is checked at
  runtime rather than assumed: a block that finds a non-nearest MXCSR must reach
  the helper, because a route that does set rounding control would otherwise
  differ silently. The measurement says the guard will not be taken here, not
  that it can be left out.
- The census itself is **temporary and has been removed**; it is not in the
  shipping runtime. The numbers above are its whole output.

## The lowering is built, and it is in the browser product

x86port `ff275c5` gives `emit_wasm` its `v128` type and `0xFD` prefix and
emits ADDPS, SUBPS, MULPS, DIVPS, ANDPS, ANDNPS, ORPS, XORPS and SHUFPS with
an xmm destination and an xmm or 16-byte memory source on the contiguous
mapping. The whole instruction becomes two loads, one operation and one store,
against the four i32 loads, eight arguments across the module boundary,
lane-at-a-time C loop and four i32 stores it replaces. MINPS and MAXPS are
deliberately left on the helper: `f32x4.min`/`max` differ from x86 on NaN and
signed zero, and nothing on the measured route runs either, so they are not
added untested. No MXCSR guard is emitted and that is a decision rather than
an omission -- the helper being replaced reads nothing from MXCSR either, so
the two are the same function, which is what the differential proves.

There is no `v128` local group. Both operands stay on the stack between their
loads and the store that consumes them, so declaring one would have put a
zeroed 16-byte slot in every block body -- including the overwhelming majority
with no SSE in them, entered hundreds of millions of times -- and would have
made every translated block require the SIMD feature from the engine rather
than only the blocks that use it.

Proved by differential against the separately linked oracle in a real engine:
1,278 cases, 18,567 checks, 0 failures, 392 of 1,270 SIMD instructions taking
the emitted path, both denominators asserted. Made to fail first: flipping
ANDNPS's operand order gives 27 failures and swapping the shuffle's two halves
gives 32.

**In the browser product**, on the Dead Zone route: `100 of 164 SIMD
instruction(s) emitted as host SIMD`. The denominator is small because it is
translation SITES, not executions -- the census above counted 317,883,827
operations from that same handful of sites. The port's heartbeat and shutdown
report now carry that pair, so a build that lowered nothing can be told from a
route with no SSE in it.

No frame-rate result is claimed for this change on its own. The run that
carried it read 12.707 +/- 0.010 presents/s at load average 5.0, which is not
comparable with the 11.552 and 11.840 readings this issue's neighbours record
at other host loads.
