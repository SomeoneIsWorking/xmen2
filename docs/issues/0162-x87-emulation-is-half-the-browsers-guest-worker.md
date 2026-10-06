---
id: 162
title: x87 emulation is half the browser's guest worker
status: resolved
symptom: x87 emulation was half the browser's guest worker; its helpers are now 1.7% of it
state_items: S021
tags: web,browser,wasm,x87,x86port,performance
created: 2026-09-19
updated: 2026-09-24
---

# 0162 — x87 emulation is half the browser's guest worker

- **State items:** S021
- **Status:** resolved. The 80-bit requirement this issue argued from was
  given up deliberately: on a host with no x87 unit the arithmetic runs in
  binary64 (`x87.double`, xmen2 `23ebe8a`, x86port `a79c395`), the precision
  Apple Silicon already had, and the wasm blocks compute FADD/FSUB/FMUL/FDIV
  in place. Measured 2026-09-24 below: every x87 helper together is 1.74% of
  the browser's busiest worker. The history below is kept because each step
  is still in the shipping path.
- **Follows:** #157 and #161, each of which removed the cost that was hiding
  this one

## The profile

Dead Zone route, browser, with the JIT code arena set past the working set so
translation is quiescent (#161). The busy worker, 35,112 working samples of its
35,821 — 98.0% of its wall time — and shares against its own samples:

| category | share |
|---|---|
| port native code (other) | 65.34% |
| translated guest block | 12.22% |
| JS glue | 7.84% |
| JIT dispatch / execution | 7.08% |
| host import stub / runtime | 2.64% |
| thread wait (spinning) | 1.11% |
| renderer | 0.95% |
| **x86port JIT translation** | **0.00%** |
| **wasm compile/instantiate** | **0.00%** |

The two that were 33% and 17% are now zero. What "port native code (other)"
mostly is, by self time:

| frame | share |
|---|---|
| `x86p_x87_arith` | 15.16% |
| `x86p_x87_read_value` | 13.59% |
| `x86p_wasm_x87_store` | 5.02% |
| `x86p_x87_software_narrow` | 3.46% |
| `x86p_x87_push` | 1.44% |
| `x86p_wasm_x87_arith_mem` | 1.32% |
| `x86p_x87_pop` | 1.13% |
| `softfloat_roundPackToExtF80` | 1.04% |
| `x86p_wasm_x87_load` | 0.97% |
| `extF80_add` | 0.91% |
| `x86p_wasm_x87_arith_reg` | 0.88% |
| `softfloat_subMagsExtF80` | 0.82% |
| `f128_to_extF80` | 0.77% |
| `softfloat_addMagsExtF80` | 0.74% |
| `softfloat_normRoundPackToExtF80` | 0.65% |
| **x87, total** | **≈47%** |

The next costs are `x86p_jit_engine_run` at 6.40% (dispatch itself),
`backing_span` at 5.66% (what is left of guest memory after #157) and
`_emscripten_get_now` at 3.79%.

**Nearly half of the browser's guest worker is emulating an x87 FPU in
software.** The game's own translated code is 12%.

## Why it is software

WebAssembly has `f32` and `f64` and nothing wider. x87's register stack is
80-bit extended precision, so x86port carries an 80-bit softfloat — the
`extF80_*` and `softfloat_*` frames above are a Berkeley SoftFloat-shaped
implementation of add, multiply and rounding in integer operations.

`x86p_x87_software_narrow` at 3.46% is not a precision-narrowing fast path: it
is the ext80-to-f128 conversion on the way back out of every arithmetic
operation, i.e. cause 1 below.

## What this is worth, stated before it is built

The same big-arena run presents 1,263 frames in 210 seconds: **about 6 fps.**
x87 is 47% of the busy worker, so removing every cent of it is a ceiling of
**1.9x, to about 11 fps** — and the realistic share of that 47% which is
overhead rather than necessary arithmetic is the ~40% above, so ~1.7x.

That is the largest single item available and worth taking, and it is **not
enough on its own** for the goal. Do not let a good x87 result be reported as
the framerate being fixed.

## The question worth asking first, and its measured answer

x87 has a precision-control field in its control word, and Win32 sets it to 53
bits — double — at process start. A game compiled in 2005 computing `float`
and `double` math through x87 arguably does not need, and by its own control
word might not ask for, 80-bit intermediate results. If this guest ran at
PC=53, the work would be to compute in the host's `f64` and keep the softfloat
only for the extended case.

**It does not. Measured: PC=64, extended, on 100.0% of operations.**

Counted in `x86p_x87_arith` over the `deadzone-render` case — over arithmetic
OPS rather than FLDCW events, because a control word set once and used a
billion times and one set a billion times and never used give the same event
count and opposite answers:

| precision / rounding | operations | share |
|---|---|---|
| PC=extended, RC=nearest | 98,348,146 | 98.35% |
| PC=extended, RC=toward-zero | 1,651,854 | 1.65% |
| anything at PC=53 or PC=24 | 0 | 0.00% |

The instrument is trusted because it showed the other answer where there was
one to show: **RC moved.** 1.65% of operations run with rounding set to
truncate, which is the guest's own `FLDCW` reaching `f->control` — so a
constant `X86P_X87_CW_INIT` default being echoed back is excluded. The
precision field genuinely never changes.

**So the narrowing idea is dead, and no code was written for it.** The guest
asks for 80-bit results and x86port must keep producing them. The remaining
work is a faster 80-bit path, not a narrower one.

## Where the 47% actually goes, which is not arithmetic

Split the profile's frames into the arithmetic and the plumbing around it:

| | share |
|---|---|
| ext80 arithmetic proper (`extF80_*`, `softfloat_*`, `f128_to_extF80`) | ≈4.9% |
| everything else (`x86p_x87_arith` self, `read_value`, `wasm_x87_store`, `software_narrow`, `push`/`pop`) | ≈40% |

**Eight times as much time goes into moving values as into computing them.**
Two causes, both readable in the source and neither touching a rounding
decision:

1. **The stored format is not the computed format.** `X86pX87` holds
   `long double reg[8]`, which on this host is IEEE binary128; arithmetic is
   `extF80_*`. So `x86p_x87_software_arith` widens both operands from f128 to
   ext80, computes, and narrows the result back, every operation — and
   `classify()` then re-examines the f128 result on every write, which its own
   comment already records as "9.5% of a profiled Android frame". ext80 *is*
   the guest's format; storing it is more faithful, not less.
2. **Every x87 memory operand resolves its permissions twice.**
   `x86p_x87_read_value` calls `x86p_mem_read_bytes`, which calls
   `x86p_mem_accessible` (walking the perms table) and then `backing_span`
   (walking it again), memcpys 4-10 bytes to a stack buffer, and reassembles
   them with a shift-or loop. `x86p_mem_resolve` already does that resolution
   once and hands back a host pointer. `backing_span` is a further 5.66% of the
   worker on its own.

Neither is a fidelity trade: both produce bit-identical results.

## Cause 2 is fixed

x86port `2ab56b4` gives the memory owner a whole-range fast path: a span
covering all n bytes is itself the proof that the range is accessible, so
`x86p_mem_read_bytes` and `x86p_mem_write_bytes` walk once instead of twice
when the range does not straddle anything. It landed first inside
`x86p_x87_read_value`, which was the wrong layer — the policy belongs to the
memory owner, where SIMD, the string operations and the instruction fetch get
it too.

In the profile, `x86p_x87_read_value` fell from 13.59% of the guest worker to
10.95% and `backing_span` from 5.66% to 3.50%.

In frames it is worth **about 6.7%**, measured over matched age windows of the
same route:

| age | baseline | one walk |
|---|---|---|
| 120-180 s | 5.73/s | 6.53/s |
| 180-240 s | 6.37/s | 6.77/s |
| 240-300 s | 6.21/s | 6.77/s |
| 300-400 s | 6.59/s | 6.67/s |
| 400-620 s | 6.46/s | 6.70/s |
| **mean** | **6.27/s** | **6.69/s** |

**The x86port commit message says 19%, and that is wrong.** It was written from
the first two windows available at the time, 60-120 s and 120-180 s, which are
where the baseline is slowest — the same mistake in shape as quoting #161's
loading interval as a steady state, made again one issue later. The full curve
also shows something the single figure hides and which is worth more than the
mean: the baseline wanders between 5.73 and 6.59 while the fixed run sits
between 6.53 and 6.77, so it settles sooner and varies less.

**And a third run settles which of those two numbers to believe.** The same
route on the same build plus the unrelated clock fix of #163, which the
profile says is worth about 0.3% of the worker:

| age | baseline | one walk | one walk + clock |
|---|---|---|---|
| 120-180 s | 5.73 | 6.53 | 6.75 |
| 180-240 s | 6.37 | 6.77 | 6.73 |
| 240-300 s | 6.21 | 6.77 | 6.35 |
| 300-400 s | 6.59 | 6.67 | 6.76 |
| 400-620 s | 6.46 | 6.70 | **5.49** |
| mean | 6.27 | 6.69 | 6.42 |

A build that cannot be slower than the one before it measures slower, and the
spread **inside** that one run — 5.49 to 6.76 — is three times the 0.42
difference between the means being compared.

**So presents/s on one run of this route cannot resolve a change of this
size.** The 6.7% above is not established, and neither is any other
single-run frame figure in this issue. What remains solid is the profile: the
two functions the change names each fell by about a fifth of themselves, and
that is not a property a noisy run produces.

Any future frame claim on this route needs repeated runs with the spread
reported, or a measurement that is not wall-clock paced at all.

## Every x87 instruction is a helper call, which is why this is 47%

`jit_wasm_x87.c` emits `x86p_wasm_call_import` for *every* x87 form -- load,
store, arithmetic against memory, arithmetic against a register, compare,
copy, exchange, the constants, the status word. Not one x87 instruction is
lowered to inline WebAssembly.

So x87 is the one instruction class the JIT does not actually compile. The
game's translated integer code is 12% of the worker; its floating-point code
is a sequence of calls into C, each of which reads the register file,
classifies a value, converts a format and calls softfloat. That is the shape
of the 47%, and it is also why the desktop build of the same JIT runs this
route at 7.42 ms/frame while the browser needs 150: on desktop `x86p_x87_arith`
uses the host's real 80-bit FPU and never enters any of this.

## The obvious escape is measured and closed: f64 is not equivalent

If the arithmetic could be done in the host's `f64` -- one WebAssembly
instruction instead of a helper call into softfloat -- the whole category
would collapse. It cannot, and this is a count rather than an argument.

A probe on the `deadzone-render` case computed each operation both ways and
compared, over **90,000,000 arithmetic operations**:

| | operations | share |
|---|---|---|
| both operands exactly representable in f64 | 81,765,989 | 90.85% |
| the ext80 result exactly representable in f64 | 81,092,372 | 90.10% |
| computing in f64 gives the **same** value | 76,887,116 | 85.43% |
| computing in f64 gives a **different** value | 13,112,884 | **14.57%** |

One operation in seven changes. That is not a rounding-noise argument to wave
through; it is a different game state. **So an unguarded f64 path is a fidelity
change, not an optimization, and it is not being taken.**

The instrument is trusted because it was made to show both answers before any
count was read: it runs a must-agree case (1.5 x 2.0) and a must-differ case
(1 / 3, where ext80 keeps eleven significand bits f64 does not) *through the
same comparison* the counts are made with, and prints UNTRUSTED above the
table if either comes out wrong. It also prints its totals unconditionally, so
"0 would agree" and "the probe never ran" are different lines -- which earned
its keep immediately, because the first version reported only at exit, the
harness kills the process, and a whole run produced no output at all.

**What the probe could not answer:** the operand-width table came back empty.
`x87_memory.c` is the WASM path; the desktop x64 backend lowers x87 memory
operands itself, so a desktop run never reaches it. Whether the guest mostly
stores f32 -- which would mean an ext80 intermediate is unobservable in most
cases -- is still unmeasured, and it needs a browser-side count.

## Cause 1 is not a small change, and here is its size

The register file would have to hold `floatx80` — the guest's own format, two
scalars — instead of `long double`. On this host `long double` is binary128,
which WebAssembly has no register for, so every pass, return and copy of one
is memory traffic: `x86p_x87_arith` takes one by value, `x86p_x87_get` writes
one out, `x86p_x87_software_arith` takes two and returns one, and
`x86p_x87_set` copies one in and then re-examines it in `classify`.

`long double` appears about 170 times across 23 files in x86port, including
all three JIT backends (`jit_x64_x87.c`, `jit_arm64_x87.c`, `jit_wasm_x87.c`),
and the desktop path depends on it being the host's real 80-bit type. So this
is core surgery on the shared repository's numeric type, not an afternoon. The
shape that would contain it is an internal value type that is `floatx80` on
binary128 hosts and `long double` where the host FPU is real, with the public
API converting only at its edges — but that is a design to write down and
review, not to start from a profile.

The two exact converters are already cheap bit shuffles
(`x86p_x87_f128_to_ext80_exact` is a handful of shifts and masks), so what
this would remove is the value traffic around them, not the conversions.

### And here is what it is worth, measured before writing it

x86port `c959f50` adds `tests/bench_x87_arith.cpp`, which runs the same three
operations three ways on a binary128 host and is not registered as a test
because it reports a ratio of wall-clock times. Under node on the wasm build,
four runs agreeing to within 0.03:

| | share of the path |
|---|---|
| converting into and out of the storage type | **38%** |
| the ext80 arithmetic itself | 38% |
| the register file's own tags, stack and status | 23% |

**So changing the storage is worth about 1.62x on the arithmetic path.** Across
the x87 frames the profile names, that projects to roughly 13% of the guest
worker, or about 1.15x overall -- real, worth taking, and not a framerate fix.

The benchmark needed three arms rather than two, and the reason is worth
keeping. With only "through the register file" against "already in ext80", the
answer came out 2.43x -- but that difference charges the storage for the tag
and stack work a storage change does not remove. The middle arm separates them.

The first middle arm was also wrong, in a way that would have closed this
question in the wrong direction: it used `x86p_x87_arith_portable`, which is
native `+ - * /` on binary128 followed by a re-round -- a different algorithm,
not arm A with a layer removed. It reported the conversion at 12% and the
register file at 44%, i.e. "the storage is not the problem". Using the function
arm A actually calls on this host, `x86p_x87_software_arith`, reverses that.

## Cause 1 is landed, and here is what it moved

x86port `ab29b41`. `X86pX87Reg` is the register file's storage: `long double`
where that type IS the x87 format, and the two architectural fields where it is
not. The raw entry points are the implementation and every `long double` entry
point is a wrapper over them, so one stack discipline, one tag classifier and
one arithmetic path survive. `jit_wasm_x87.c` -- the per-instruction helpers --
now stays in the storage type from guest memory to guest memory.

In the browser, two 20-second windows of the same route, which agree closely
enough to trust (`read_value` 10.17/9.95, `arith` 8.62/8.58):

| frame | before | after |
|---|---|---|
| `x86p_x87_arith` | 15.16% | **8.6%** |
| `x86p_x87_software_narrow` | 3.46% | 2.1% |
| `x86p_x87_push` | 1.44% | below the top 30 |
| `f128_to_extF80` | 0.77% | **absent** |
| `x86p_wasm_x87_store` | 5.02% | 4.6% |
| `x86p_x87_read_value` | 10.95% | 10.1% |

The three frames the change targets each fell by a third to a half, and the
binary128-to-ext80 conversion left the profile entirely. In isolation
`bench_x87_arith` puts the arithmetic path at **1.44-1.47x**.

**What this table is NOT is a total, and the reason matters.** The partition
changed: `x86p_mem_read_bytes` (6.2%) and `x86p_mem_write_bytes` (3.0%) now
appear as named frames where they had been folded into their callers, so
summing "x87 frames" across the two builds compares different partitions --
the same trap recorded in #163. x87 is roughly 38-40% of the worker after,
against ~47% before, and that comparison carries this caveat rather than being
clean.

**And it is not a frame rate.** This route's presents/s has a within-run spread
three times larger than the effect being looked for, which is why every frame
figure in this issue was retired. Nothing here claims one.

## What is next, and the profile now names it plainly

The single largest frame is no longer arithmetic. It is
`x86p_x87_read_value_raw` at ~10%, and with `x86p_mem_read_bytes` at 6.2% and
`backing_span` at ~3.9% behind it, **roughly a fifth of the guest worker is
spent fetching x87 memory operands** -- typically four bytes at a time, through
a helper call, a permission walk and a conversion.

WebAssembly has `f32.load`. The backend could emit the load inline and hand the
helper the bits, leaving the walk only for addresses the contiguous mapping
does not cover. That is a larger structural win than the one just landed, and
it is the next move.

`x87_load_is_emittable` also still refuses `FLD m80`, and the comment in
`jit_x87_predicates.c` naming host-independent f80 storage as the proper fix is
now satisfied -- the storage IS ext80. Lifting that refusal needs its own
admission tests and is a separate change.

## The inline load landed, and the walk is gone from the read path

x86port `98cc6ab`. FLD/FILD, the memory arithmetic forms and FCOM/FICOM now
emit `x86p_wasm_state_guard` -- the same inline bounds check, two page
permission bytes and early return every integer load gets -- and then the load
itself, and hand the helper a value rather than an address. Width 8 crosses as
a low/high i32 pair because every import in that module is i32-only.
`x86p_x87_reg_from_operand_bits` is the conversion table both routes share, so
`x86p_x87_read_value_raw` still answers for the interpreter without a second
copy of the width rules.

Two 20-second windows, same route, agreeing to within a tenth of a point on
every frame (`x86p_x87_arith_raw` 10.66/10.61, `reg_from_operand_bits`
8.12/8.05, `wasm_x87_store` 5.23/5.24):

| frame | before | after |
|---|---|---|
| `x86p_x87_read_value_raw` | 10.1% | **absent** |
| `x86p_mem_read_bytes` | 6.2% | **absent** |
| `backing_span` | 3.9% | 1.18% |
| `x86p_x87_reg_from_operand_bits` | — | 8.1% |
| `x86p_wasm_x87_load_bits` | — | 2.0% |
| `x86p_wasm_x87_arith_mem_bits` | 1.32% (`arith_mem`) | 1.7% |

**Read the new 8.1% carefully: it is not new work.** `reg_from_operand_bits`
inlines `x86p_x87_reg_from_f32_bits`/`_f64_bits`, which on this host are
softfloat `f32_to_extF80`/`f64_to_extF80`. That conversion was always there,
inside `read_value_raw`; it is now the frame that carries it. What actually
disappeared is the fetch around it -- the helper call, the permission walk, and
`x86p_mem_read_bytes`, which no longer has a reader on this path at all and
survives in the profile only as `x86p_mem_write_bytes` for stores. Counting
only what left: about 6.2 points of `mem_read_bytes` and 2.7 of `backing_span`.

Shares are of a smaller total afterwards, so a frame doing the same work reads
higher: `x86p_x87_arith_raw` at 10.66% against 8.6% before is that, not a
regression. This is the third time the partition has moved in this issue and
the caveat has not changed.

**The frame counter, for once, moved further than its own noise -- and it is
still one run against one run.** Presents per five seconds across this run:
41, 42, 45, 46, 44, 44, 36, 46, 32, 37, 44, 45, 45 -- 6.4 to 9.2 presents/s,
mostly above 8.4. The run recorded in #163 went 6.75, 6.73, 6.35, 6.76, 5.49.
The distributions barely touch, which is more than the ~20% floor this route
has shown before, but nothing here re-ran the old build, so it is an
observation and not a measurement.

The run stayed dynarec-clean throughout: 61,212 blocks translated, **0
refusals**, 0 evictions, product fallback unavailable.

## The store went inline too, and now no x87 memory route walks the mapping

x86port `fe196cd` did for the store what `98cc6ab` did for the read, and it
needed one thing the read did not. A read may fault before it does anything; a
store may not. FIST of a value that does not fit records an invalid operation in
the x87 status word, and the interpreter records it whether or not the address
turns out to be writable, so an early return out of the block would lose the
flag. The verdict therefore travels as a VALUE: `x86p_wasm_state_check` emits
the same bounds and permission proof the guard emits and pushes 1 or 0 instead
of returning, and `x86p_wasm_x87_store_at` converts first and consults it
second.

Two 20-second windows of the same Dead Zone route, taken back to back, against
the build the browser actually fetched (9,655,882 bytes served, matching
`build/release/web/x2native.wasm`, symbol map containing `x86p_wasm_x87_store_at`):

| frame | before | after (window 1 / window 2) |
|---|---|---|
| `x86p_wasm_x87_store` | 5.24% | **absent** |
| `x86p_mem_write_bytes` | 3.36% | **absent** |
| `backing_span` | 1.18% | **absent** |
| `x86p_wasm_x87_store_at` | — | 3.61% / 3.67% |
| `x86p_x87_operand_bytes_from_reg` | — | 2.01% / 2.02% |

The absence is checked by name and not by reading off a top-N list: no symbol
containing `x86p_mem` appears anywhere in a 400-deep listing of either window.
The same listing still resolves `x86p_x87_reg_from_operand_bits` and every other
wasm frame, so it is capable of printing the other answer.

**Again, read the two new frames as one.** 3.6 + 2.0 = 5.6 points where 5.24 +
3.36 = 8.6 stood, and `operand_bytes_from_reg` is the conversion that was
already inside `wasm_x87_store` -- extracted so the sparse and contiguous
routes share it, which is also why it now has a frame of its own. What left is
the walk: `mem_write_bytes` entirely, and the last of `backing_span`, which no
longer has any caller on this route at all.

Presents per five seconds, before the profiler was attached: 48, 47, 49, 49 --
9.4 to 9.8 presents/s, against 6.4 to 9.2 on the read-inline build and 5.5 to
6.8 in #163. Under the profiler it fell to 43, which is the sampler. The same
caveat as every earlier entry: nothing re-ran the older builds side by side, so
this is an observation of a trend and not a controlled measurement. The run
stayed dynarec-clean: 61,239 blocks translated, 0 refusals, 0 evictions, 0
flushes, product fallback unavailable.

## What is next after that

No x87 memory route resolves an address at runtime any more, so the remaining
cost is arithmetic and dispatch:

- `x86p_x87_arith_raw` 10.6% plus its softfloat leaves (`extF80_add`,
  `softfloat_subMagsExtF80`, `softfloat_roundPackToExtF80`,
  `softfloat_addMagsExtF80`, `softfloat_normRoundPackToExtF80`) -- about 17% of
  the worker for ext80 arithmetic.
- `x86p_jit_engine_run` 8.5% (dispatch). Every conditional branch in every
  translated block called `x86p_cond` through an import. **Fixed — see #168**,
  which derives 96.2% of them inline in the running product. The reading that
  sent this bullet looking at a zeroed counter was itself wrong: the heartbeat
  said "0 of 35,996 lowered inline" because the page was running the *previous*
  build out of a service-worker cache, and #168 records that trap.
- `x86p_x87_reg_from_operand_bits` 8.4% and `x86p_x87_operand_bytes_from_reg`
  2.0% are the f32/f64 <-> ext80 conversions. They are the price of holding
  guest state in the guest's own format on a host with no 80-bit register, and
  `6c500ea` measured what computing in f64 instead would cost: one result in
  seven.
- `x86p_x87_pop` 1.9% is still a separate import call per FSTP. Folding the pop
  into the load, store, arithmetic and compare helpers removes a crossing per
  instruction for the commonest x87 form in this route.

## What would falsify the attribution

The profile is one 15-second window of one route with translation quiescent. If
a window taken during combat, or on another map, puts x87 well below 47%, then
this is the cost of one scene's math and not of the game's. Worth taking a
second window before building anything.

The precision measurement has its own falsifier and it is narrower: it counts
the `deadzone-render` case on the DESKTOP build. The guest's control word is
the guest's own and does not depend on the host, but if a browser-side count
ever shows PC=53 traffic that the desktop case never reaches, the table above
is this route's answer and not the game's.

## The widening is out of the softfloat, and the attribution now has a denominator

`x86port 70e6536`. FLD m32 and FLD m64 cannot round -- every binary32 and
binary64 value has an ext80 with the same number in it -- yet both were calling
the general softfloat conversion, which exists to make the rounding decision
this direction does not have. `x87_ext80_widen` owns the reassembly now.

| frame | before | after |
|---|---|---|
| `x86p_x87_reg_from_operand_bits` | 4.95% | **2.90%** |

The residue is the width dispatch and the integer operand path, with the
widening itself inlined into it.

That is the first change measured against a proper denominator rather than a
top-N list. Categorising the guest worker's samples by owner, over a 25-second
window of the Dead Zone route:

| category | share of the guest worker |
|---|---|
| x87 emulation | **39.1%** |
| all translated guest code | 21.9% |
| dispatch (`jit_engine_run` + `intercept`) | 12.4% |
| flags and ALU helpers | 4.5% |
| SSE/SIMD helpers | 4.2% |

x87 is not merely the biggest frame in a list; it is more than every translated
guest instruction in the game put together. Nothing else on this route is worth
starting while that is true.

## The frame rate did not move, and that is not a contradiction

11.441 +/- 0.022 before, 11.384 +/- 0.014 after. Both runs' plateaus are the
same 11.40 - 11.60 band, and the two numbers were taken at load average 11.5
and 4.5 on a shared machine, which moves the rate by far more than two percent.

**A two percent change is below what this host can resolve in presents/s**, and
saying so is the point: a five-second window quantises the rate at one frame,
which on this route is 1.75%, so a real two-percent win lands inside the band it
started in. `tools/web_presents.py` now also reports a steady rate counted once
across the whole plateau, with the tolerance of a single frame over the span,
which is what makes a number like 11.384 +/- 0.014 sayable at all. The profile
is the instrument for a change this size; presents/s is the instrument for a
change like `8b18aab`'s eighteen percent.

## The route is guest-CPU-bound, and that was worth proving

The flat 11.4 raised the obvious worry: if something else caps the frame rate,
every CPU saving is invisible and the whole plan is wrong. It does not.

In one run the machine took about half the worker away mid-capture. Guest block
entries per five-second window fell from 18.4M to 9.8M, and presents/s fell from
11.0 to 5.8 in the same windows -- 47% against 47%. Frames on this route track
the guest worker one for one, so the category table above is a roadmap and not
just an accounting of where time sits.

## The pop crossed the boundary a second time, and no longer does

x86port `30ad283`. An x87 instruction that retires a stack slot used to cost
two import crossings: the helper did the operation and returned a verdict, the
emitted block tested it, and on success it called `x87_pop` back across the
boundary, once per slot. `FSTP` is the commonest x87 form on this frame, so
most x87 stores were paying it.

The helper already knows whether it completed, so it takes the pop count and
retires the slots itself, on its success path only. Seven helpers gained the
parameter. `arith_mem_bits` reaches eight in doing so, which is the import
ABI's hard arity cap -- `write_types` declares an i32 return for one through
eight parameters -- so there is no room left at that helper and a ninth
argument would need the ABI widened first. That is a fact about the next change
here, not a problem with this one.

### What the profile says

Guest worker, Dead Zone route, 25s, 96,169 samples:

| frame | before | after |
|---|---|---|
| `x86p_x87_pop` | 1.89% | **absent** |
| `x86p_wasm_x87_store_at` | 3.10% | 3.51% |
| `x86p_wasm_x87_load_bits` | 2.69% | 2.73% |
| `x86p_wasm_x87_arith_mem_bits` | 2.09% | 2.02% |
| `x86p_x87_reg_from_operand_bits` | 2.90% | 2.62% |
| `x86p_x87_operand_bytes_from_reg` | 3.99% | 4.30% |

The helpers that absorbed the work grew by about 0.4 points between them, which
is the pop's actual arithmetic; the rest of the 1.89% was the crossing. The two
columns come from runs at different host loads, so treat the individual rows as
having a few tenths of slack -- the disappearance does not.

**The absence is a measured zero and not a resolution failure.** `x86p_x87_pop`
is still in the module at symbol index 5472, because the compare-integer site
still calls it and that pop is not conditional on a helper's verdict, and the
profile's symbol map resolves it. A listing 200 frames deep does not contain it.
The tool can name this function and does not.

### What is next in this issue

The ranking inside x87 is now `x86p_x87_arith_raw` at 14.57%, then the operand
plumbing: `operand_bytes_from_reg` 4.30%, `store_at` 3.51%, `load_bits` 2.73%,
`reg_from_operand_bits` 2.62%, `arith_mem_bits` 2.02%, `arith_reg` 1.53%. The
softfloat's own internals -- `subMagsExtF80` 2.04%, `roundPackToExtF80` 1.27%,
`addMagsExtF80` 1.15% -- sit under `arith_raw` and are the floor that the
PC=extended measurement above says cannot be traded away for f64.

### The frame rate, with the caveat it always carries

11.552 +/- 0.011 per second, 1042 presents over 90.2s, plateau band 11.40 -
11.80, at load average 5.0. The previous build read 11.384 +/- 0.014 at load
average 4.5 and its band topped out at 11.60.

That is +1.5%, which is the right size and the right direction, and it is still
not proof on its own: a shared host moves this route by more than 1.5% between
runs, which is exactly why the disappearance of `x86p_x87_pop` from the profile
is the evidence and the frame rate is the corroboration.

## The load no longer leaves the module, and that is 2.83 points

x86port `0ddf304`. `x86p_x87_reg_from_operand_bits` had already been taken out
of the softfloat and reduced to shifts (`70e6536`), and it was still 2.62% of
the guest worker with `x86p_wasm_x87_load_bits` at 2.73% in front of it. The
remaining cost was not arithmetic. A translated block is its own WebAssembly
module, so reaching that helper is a cross-module call, and the guest makes one
per FLD.

The ordinary case is now emitted into the block. Three integer tests decide it
-- the stored exponent is neither zero nor all ones, and the destination
register is empty -- and a mask, an OR, a shift, a rebias and three stores do
it. Everything the tests reject goes to the helper unchanged: subnormals,
zeroes, infinities, NaNs, integer operands, and a push onto a full stack, which
sets three status bits and stores nothing.

### What the profile says

Guest worker, Dead Zone route, 25s, 85,786 working samples of 88,387 (97.1% of
its wall time). The before column is the pop-fusion table above.

| frame | before | after |
|---|---|---|
| `x86p_wasm_x87_load_bits` | 2.73% | **0.65%** |
| `x86p_x87_reg_from_operand_bits` | 2.62% | **1.87%** |
| the two together | 5.35% | **2.52%** |

**2.83 points of the guest worker.** The two columns come from runs at
different host loads and the rest of this issue's tables carry a few tenths of
slack for that; a 2.08-point fall in one row is an order of magnitude outside
it.

Neither row goes to zero and neither should. What is left in
`reg_from_operand_bits` is FILD -- an integer operand is a different conversion
and still crosses -- plus the cold float cases; what is left in `load_bits` is
those same crossings. The guest's own ratio says how much was reachable: of the
3,396 memory x87 load sites this route translated, 3,161 took the emitted form
and 235 did not, which is the FILD forms and 93.1%.

The work moved rather than vanishing, and the profile shows that too: the
`translated guest block` category is 24.48% here against 12.22% in the table at
the top of this issue. That is what inlining looks like from the outside.

### The frame rate is not the instrument for this and was not usable today

Presents/s was measured on both builds and the numbers are not reportable. A
baseline run plateaued at 11.40 - 12.00 (steady 11.600 +/- 0.040) and two runs
of the new build read 12.00 - 12.20 (steady 12.033 +/- 0.033) and 8.80 - 12.00
(steady 11.155 +/- 0.040). The host was at load average 18.8 with eight
concurrent `clang++` processes and a VM belonging to other work, which moves
this route by far more than the effect being looked for. The section above on
the pop fusion already states the general form of this: a change of this size is
below what presents/s can resolve on a shared machine, and the profile is the
instrument. Nothing here claims a frame-rate result.

### How the two implementations are held together

There are now two implementations of the same widening, one in C and one
emitted, and that is the cost of this change. `x86p_ext80_source` publishes the
six numbers that say where a binary32 or binary64 operand's fields are, and both
read it; splitting an operand into sign, exponent and fraction is written once
in C as well. `tests/test_wasm_x87.c` runs the emitted form against the
interpreter over fourteen operand values chosen to separate the arms -- both
ends of the normal range, both zeros, three subnormals, both infinities, a quiet
and a signalling NaN -- at four stack depths including the full one, on the
contiguous and the sparse mapping. A rebias wrong by one fails 65 of its checks;
that was run, not reasoned about. The suite also asserts both denominators: 289
x87 loads lowered, 250 inlined, so neither "nothing was inlined" nor "the
declined forms were never lowered here" can pass.

### What is next in this issue

The ranking inside x87 is now `x86p_x87_arith_raw` at 14.90%, then
`operand_bytes_from_reg` 4.28%, `store_at` 3.60%, `arith_mem_bits` 2.25%,
`reg_from_operand_bits` 1.87%, `arith_reg` 1.59%, `copy` 0.71%, `load_bits`
0.65%. The store side is now the larger half of the operand plumbing and has
the same shape the load side just had: `store_at` calls
`operand_bytes_from_reg` across the module boundary on every FST. The narrowing
it performs is not the load's shift-and-rebias -- it rounds, and consults the
control word to do it -- so the inline arm there is a smaller subset of cases
than this one, and worth sizing before it is written.

## The store side is emitted too, and the operand plumbing is gone

The section above sized it and said the inline arm there would be a smaller
subset than the load's. It is: a store ROUNDS, so only round-to-nearest-even
on a normal ext80 value whose result is a normal in the target can be done
with integer operations. x86port `cc924a1` emits exactly that, and everything
else -- a zero, a subnormal either side, an infinity, a NaN, an unnormal, an
integer or 80-bit destination, a non-nearest RC, the sparse mapping, an
address the guard refuses, and a rounding carry out of the significand --
reaches the helper that answered before.

The acceptance rule is its own module, `x87_ext80_narrow.h`, because it now
has two implementations: a C fast path in `x86p_x87_reg_to_f32_bits` /
`_to_f64_bits`, which every host gets and not only the browser, and the
emitted WebAssembly. A carry is refused on BOTH sides even though C could
finish it cheaply, because one rule implemented twice is only safe while the
two rules are the same one.

### What the profile says

Guest worker, Dead Zone route, 25 s, 86,765 working samples of 89,154 (97.3%
of its wall time). The two frames this change is about are **absent from the
profile entirely**:

| frame | before | after |
|---|---|---|
| `x86p_x87_operand_bytes_from_reg` | 4.28% | **not in the profile** |
| `x86p_wasm_x87_store_at` | 3.60% | **not in the profile** |

The work moved rather than vanishing, and the profile shows that the same way
the load side did: `translated guest block` is 28.30% here against 21.9%
before. Every other x87 row's share rose without its cost changing, which is
what happens to a share when the denominator loses about eight points --
`x86p_x87_arith_raw` reads 16.71% against 14.90%, and 14.90/0.92 is 16.2.

In the product, `3,906 of 3,932 x87 store(s) narrowed in the block` -- 99.3%
of the sites this route translates. The 26 that decline are the integer and
80-bit forms.

### No frame figure, again, and this time the host says why

The run carrying this read a plateau of 13.042 +/- 0.025 presents/s, at load
average 35.67 with its own profiler and other agents' builds on the machine,
against 12.707 +/- 0.010 at load average 5.0 for the build before it. Those
two numbers are not comparable in either direction and neither is offered as
a result. The profile is.

### What is next in this issue

`x86p_x87_arith_raw` is now 16.71% of the guest worker and the softfloat under
it another 5.2% (`subMagsExtF80` 2.27%, `roundPackToExtF80` 1.70%,
`addMagsExtF80` 1.25%), with `arith_mem_bits` 2.44% and `arith_reg` 1.82% of
argument plumbing around them. That is the arithmetic itself rather than the
moving of values, and it is the part this issue has not touched.

## The obvious idea for the arithmetic itself, tried and measured: it loses

The section above leaves `x86p_x87_arith_raw` at 16.71% of the guest worker
with the softfloat under it at another 5.2%, and names that as the part this
issue has not touched. The obvious attack is to stop emulating the operations
the host could perform: the guest computes with a 64-bit significand and
binary64 has 53, but an operation whose TRUE result binary64 holds exactly has
the same answer in both, bit for bit, because a result that needs no rounding
is rounded identically by every format wide enough to hold it and by every
rounding mode.

That was built (x86port `230694f`, `c5e6c95`) and reverted (`1747281`). It is
recorded here because the reasoning is attractive enough that it will be had
again.

**The rule was right.** It required 64-bit precision control, operands that are
normals or zeros binary64 holds exactly, and a normal non-zero result -- zero
refused because the sign of an exact zero difference belongs to the rounding
mode -- and it PROVED exactness rather than assuming it: Knuth's two-sum error
for addition, significand bit counts for multiplication, a multiply-back for
division. Checked against both full-precision authorities, the host's own x87
unit and the Bochs softfloat under Emscripten: 16,129 accepted operations,
**0 divergences**, on both.

**It was faster than what it replaced, where it applied.** Under node, 15.0 ns
per accepted operation against the softfloat's 27.1 ns -- 1.81x.

**It still lost the frame rate.** A refused operand pays the rule and then the
softfloat anyway, at +13.7%, which puts break-even at about 21% acceptance:

| build | presents/s | machine load |
|---|---|---|
| with the fast path | 13.64 | 6.2 |
| without it | **13.83** | 7.4 |

The baseline carried the higher load of the two and still won. The profile
agreed -- `x86p_x87_arith_raw` read 26.60% of the guest worker with the fast
path against 16.80% without -- but a profile could not have decided this on its
own: the softfloat's own frames left the profile in both readings, which is
equally consistent with "the fast path takes every operation and costs more"
and with "the softfloat was inlined into its caller and the shares moved". The
frame rate is what separated them.

**So the acceptance rate on this route is below 21%**, which is the finding
worth keeping: this title's x87 population is mostly NOT exact in binary64. A
game's floats arriving from memory as binary32 does not make its arithmetic
binary32-shaped -- one division, one transcendental or one sum across a wide
exponent range puts a full 64-bit significand into a register, and every
operation downstream of it is refused.

What this does NOT rule out: a register file that keeps values in binary64
while they stay exact, which would remove the per-operation conversion rather
than the arithmetic. That idea inherits the same acceptance question and must
measure it FIRST -- the number above says the answer is probably no.

## The softfloat itself is close to its floor on this target

Before writing a specialised ext80 multiply, it was benchmarked against the one
Bochs ships, under node, on the shapes this title actually multiplies:

| operands | Bochs `extF80_mul` | a hand-written normal-operand multiply | ratio |
|---|---|---|---|
| binary32-derived | 13.6 ns | 10.6 ns | 1.28x |
| full 64-bit significands | 14.0 ns | 7.6 ns | 1.85x |

The hand-written arm agreed with Bochs on every value; it differed only by not
setting the rounding-up bit, which is a line to add rather than a reason.

1.28x on the operands the game supplies is not the lever it looked like. The
reason is structural and worth writing down so it is not re-derived: **WebAssembly
has no 64x64 to 128 multiply**, so the one instruction an x87 unit spends on a
significand product becomes about twenty, and that cost is in both arms. A
scalar ext80 multiply on this target has a floor near 8 ns whoever writes it.

Per-operation cost of the shipping path, same conditions: add 16.5 ns, sub
20.4 ns, mul 15.6 ns, div 19.5 ns. Of that, about 3.5 ns is the adapter
building a `softfloat_status_t` per call rather than the arithmetic.

So the remaining x87 work is the PLUMBING, not the arithmetic: the profile
puts `x86p_wasm_x87_arith_mem_bits` at 2.12% of the guest worker,
`x86p_x87_reg_from_operand_bits` at 1.87%, `x86p_wasm_x87_arith_reg` at 1.01%
and `x86p_wasm_x87_copy` at 0.53% -- about six points spent moving operands
into and around a computation that costs twenty-two.

## What the route's x87 arithmetic actually IS, counted

Every figure above about which cases matter was an inference from a static
population or from a bench's own operands. x86port `91bdfa5` counts the real
thing: an op census on the x87 unit, off by default, armed with
`--set x87.census=1`, which records each arithmetic operation the run performs
and asks the encoding-level rules themselves whether they can answer it.

Dead Zone route, `#test-play`, 158.0M operations:

| operation | share of the route's arithmetic |
|---|---|
| FADD | 52.5% |
| FMUL | 35.9% |
| FSUB | 11.5% |
| FDIV | 0.2% |

**FDIV is 0.2%.** Every per-operation cost in the table above weighted them
equally, and a divide costs the most of the four; it is worth nothing here.

And why an operation could not be answered by an integer rule, which is the
number that decided what to build:

| | share |
|---|---|
| both operands normal | 54.4% |
| **at least one operand a ZERO** | **44.6%** |
| a control word the rules do not handle (RC or PC) | 1.1% |
| a subnormal, unnormal, infinity or NaN | **3 operations in 158 million** |

The last row is the one to read twice. This title's x87 arithmetic contains
essentially no special values at all -- the softfloat's entire handling of
subnormals, infinities and NaNs is being paid for, per operation, by a route
that reaches it three times in a hundred and fifty-eight million.

A zero operand needs no arithmetic: times anything finite it is a zero of the
combined sign, added it is the other operand exactly. Taking zeros moves what
an integer rule can answer from 54.4% to **99%** of the route, which is what
x86port `ded8126` does.

The census counts what it can ANSWER rather than what merely looks eligible:
it calls the rules instead of repeating their preconditions, because a second
copy of those is how a census comes to report headroom that does not exist.
The gap it leaves -- eligible and unanswered -- is the work not done, and it
names FDIV explicitly so a missing rule cannot read as a refusal.

## The rules in the shipping path: +2.9%, and the control is what proves it

x86port `93de3e9` sends the ordinary cases to the encoding rules and leaves
everything else on the Bochs call. Three builds over the same route, medians
across about forty steady five-second windows each:

| build | rule coverage of the route's arithmetic | median presents/5s | IQR | presents/s |
|---|---|---|---|---|
| baseline | 0 | 69 | 68-69 | 13.80 |
| multiply only | 12.5% of operations | 69 | 68-70 | 13.80 |
| **mul + add + sub + zeros** | **99% of operations** | **71** | **70-72** | **14.20** |

**Medians, not means, and the reason is a mistake worth not repeating.** The
first reading of this run used the mean of the last thirty windows and reported
12.21 presents/s -- a large regression -- because the run had a seven-window
stall in the middle from unrelated load on the machine, and because a run of a
different length puts "the last thirty windows" over a different part of a route
that is not uniform. The per-window sequence showed it immediately: 70-73
throughout, 24-30 for seven windows, 70-73 again.

**The middle row is the control.** The same change at an eighth of the coverage
reproduces the baseline exactly. That is what says the gain belongs to the added
coverage rather than to the machine, and it is also why the multiply-only
attempt was recorded here as a null result rather than as a small win -- it was
one.

What is left for x87 is the plumbing, unchanged by this: the call, the operand
conversion and the register-file bookkeeping around an arithmetic that no longer
dominates its own path. That wants the rules emitted into the block, and these
rules are what such an emitter must match.

## The profile after the change, and it re-ranks what is left

Guest worker, Dead Zone route, 150s sample, the landed build:

| frame | share |
|---|---|
| `x86p_jit_engine_run` | 12.48% |
| **`x86p_x87_arith_raw`** | **12.24%** |
| `x86p_ext80_add_ordinary` | 4.15% |
| `x86p_wasm_x87_arith_mem_bits` | 2.75% |
| `x86p_flag_cf` | 2.38% |
| `x86p_x87_reg_from_operand_bits` | 2.27% |
| `x86_engine_jit_intercept` | 1.65% |
| `x86p_wasm_x87_arith_reg` | 1.54% |
| `x86p_ext80_mul_ordinary` | 1.14% |
| `x86p_wasm_x87_load_bits` | 0.73% |

**Bochs is gone from the profile.** Its frames were 5.25% before and do not
appear in the top twenty-two now. x87 arithmetic as a whole went from about
22.0% to about 17.5% -- a 4.5-point drop in the guest worker, which is the
shape a 2.9% frame-rate gain should have, since the worker is not all of frame
time.

**The number that decides what comes next is the 12.24%.** The arithmetic now
sits in two named frames totalling 5.3%. What is left in `x86p_x87_arith_raw`
is the operand fetch, the reverse swap, the tag and status bookkeeping, and the
register-file read and write -- with `arith_mem_bits`, `reg_from_operand_bits`
and `arith_reg` adding 6.6% more of the same. Call it **about 19% of the guest
worker spent moving operands around a computation that now costs 5.3%.**

The plumbing is three and a half times the arithmetic. Be careful with the
precise figure -- some of the rules may be inlined into `arith_raw`, so 12.24%
is an upper bound on its bookkeeping rather than an exact split -- but the
ranking does not depend on the precision.

That is what an inline emitter removes, and it is roughly twice what block
chaining's whole ceiling is (#166, about 9%). **The next x87 work is emitting
these rules into the block, not making them faster.** They are already proven
against hardware and against Bochs, which is exactly what such an emitter needs
to match.

## And the plumbing gives up most of what it costs: +4.3% in total

The fused entry point (x86port `c38c5ad`) does the ordinary operation in the
format the rules take, reading and writing the register file's fields in place
instead of copying `X86pX87Reg` values through `x86p_x87_get_raw` and
`x86p_x87_set_raw`. The memory form also skips `x86p_x87_reg_from_operand_bits`
by widening the operand straight into the encoding. The long path is untouched
and still answers everything the rules refuse.

Medians over about fifty steady five-second windows each:

| build | median presents/5s | IQR | presents/s | against baseline |
|---|---|---|---|---|
| baseline | 69 | 68-69 | 13.80 | — |
| multiply only (the control) | 69 | 68-70 | 13.80 | 0% |
| the rules inside `arith_raw` | 71 | | 14.20 | +2.9% |
| **the rules through the fused path** | **72** | **71-73** | **14.40** | **+4.3%** |

The interquartile ranges of the first and last rows do not overlap.

**What a fused path gets wrong is the tag and the padding, not the answer.**
The register file is compared as memory by the WASM differential, so a write
that left the six padding bytes of an `X86pX87Reg` alone would differ from the
long path on bytes no value depends on. `tests/test_x87.c` therefore runs both
paths from an identical state and compares the whole `X86pX87` with memcmp, and
asserts four cases taken and two refused -- a version that always returned 0
would make every value comparison pass while proving nothing.

The census had to move with it, and this is the general point rather than a
detail: it lived inside `arith_raw`, which the fused path bypasses, so counting
only there would have made the instrument quietly under-report the moment this
landed. An instrument on one of two paths is an instrument that lies.

## The desktop's own number, and what it is made of (2026-09-22)

The browser is where this started; the native product is on the same helpers,
and the Dead Zone gameplay route says so. A 60 s profile of the shipping
binary attributes **58% of cycles** to the x87 family:

```
21.4%  x86p_x87_arith_raw      7.2%  x86p_x87_push
 7.2%  x86p_x87_to_f32         7.2%  x86p_x87_get
 4.2%  jit_x87_arith           3.0%  jit_x87_push
 2.4%  jit_x87_arith_reg       2.0%  jit_x87_to_f32
 1.7%  x86p_x87_set            1.6%  x86p_x87_pop
```

The `jit_x87_*` entries -- 12% between them -- are the trampolines the x64
backend calls. The route performs **750,000-820,000 guest x87 arithmetic
operations per frame** (the op census, armed) against **~305 M host
instructions per frame**, so an x87 arithmetic operation costs on the order of
a hundred host instructions before anything else is counted.

Read the fast path in the shipping binary rather than guessed at:

* The control word is **not** the cost. `x86p_x87_arith_raw` compares the
  guest's word against `fnstcw` and takes a plain `faddp`/`fmulp` when they
  agree; the `fldcw` sandwich is the other arm. The census says the guest runs
  at 64-bit extended, round-to-nearest on **99.6%** of operations, which is
  the host's own default -- so that arm is essentially never taken. A change
  to hold the host word at the guest's value would have bought nothing, which
  is why it was read before it was written.
* What the path does pay, per operation: a call from translated code, two
  `fldt` ten-byte loads, a branchless operand swap for the reverse forms, a
  `fucomi` invalid-operand check, the arithmetic, an `fstpt` into the register
  file, a SECOND `fstpt` to a stack slot purely so the tag can be read back
  out of the bytes, and -- because the ABI wants the x87 stack empty at a
  return -- four `fldz` and four `fstp` on the way out.

**The tag is not where the money is either, measured.** Keeping the class
lazily and computing it only in the packer (which is the only place the
architecture can observe it) removes that second `fstpt` and the loads that
follow it. On this route it moved nothing that could be told from run-to-run
spread: 285.3 and 306.7 M instructions/frame against 306.5 and 303.8 for the
current code, with the profile shares unmoved. The change was reverted; the
test it showed was missing was kept (x86port `9c8a2d7`).

**What remains is the call itself.** The x64 backend emits a call per x87
instruction; the WASM backend already inlines the load form
(`jit_wasm_x87_load.c`, and the heartbeat's `x87 load(s) widened in the block`
counter). Inlining the arithmetic and store forms on x64 -- keeping ST(0) in a
host x87 register across a block rather than storing it to the file and
reloading it for the next instruction -- is the next real step, and it is the
one thing in the list above that removes the per-operation constant rather
than shaving it.

### The harness cannot see a 5% change on this route

Recorded because two sessions have now read per-frame numbers as if they were
repeatable. Same binary, same route, 30 s windows after 300 presented frames:
306.5 and 303.8 M instructions/frame on one arm, 285.3 and 306.7 on another
that differs by a change the profile says is not there. Normalising by the
guest's own work does not rescue it: instructions per x87 arithmetic operation
came out 439.5 and 415.5 on two runs of the SAME binary, and those runs had
the census armed, which puts four `fstpt`/`fldt` pairs into the hot function
and inflates the figure besides. A change worth less than about 7% on this
route needs a deterministic workload, not this one.

## Resolved: what x87 costs the browser now (2026-09-24)

`#test-play`, 10,069,591-byte wasm, a 20 s V8 profile after 90 s of play.
The busiest worker has 65,313 working samples of 70,632. Every function with
x87 in its name, plus the softfloat conversions, comes to **1.74%** of them.
The largest are `x86p_wasm_x87_copy` 0.30%, `x86p_x87_exchange` 0.29%,
`x86p_x87_status` 0.27% and `x86p_x87_get` 0.23%. Translated guest blocks,
which now hold the in-place arithmetic, are 65.2% of the worker. The x87 work
inside them cannot be separated from the rest of the guest's code, and no
single helper is left to remove. `x87.double=0` restores the exact path for
a comparison.
