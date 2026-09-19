# 0162 — x87 emulation is half the browser's guest worker

- **State items:** S021
- **Status:** measured and attributed, and both escapes are now closed by
  count. The guest runs at PC=extended on 100% of operations, and computing in
  f64 instead changes 14.57% of results, so x86port must keep producing 80-bit
  answers. What remains is making the 80-bit path cheaper. Four changes have
  landed: the storage change (x86port `ab29b41`), which cut `x86p_x87_arith`
  from 15.16% of the browser's guest worker to 8.6%; the inline operand load
  (x86port `98cc6ab`), which removed `x86p_x87_read_value_raw` and
  `x86p_mem_read_bytes` from the profile entirely; the exact widening (x86port
  `70e6536`), which took `x86p_x87_reg_from_operand_bits` from 4.95% to 2.90%;
  and the pop fusion (x86port `30ad283`), which removed `x86p_x87_pop`'s 1.89%
  by ending the second import crossing per popping instruction. What remains is
  `x86p_x87_arith_raw` at 14.57% and the operand plumbing beneath it.
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
- `x86p_jit_engine_run` 8.5% (dispatch). The heartbeat says **0 of 35,996
  conditions lowered inline**: `jit_wasm_lower.c` sets `out->cond_inline = 0`,
  so every conditional branch in every translated block calls `x86p_cond`
  through an import. That is a structural move the other backends already made.
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
