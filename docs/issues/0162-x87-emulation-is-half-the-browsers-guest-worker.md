# 0162 — x87 emulation is half the browser's guest worker

- **State items:** S021
- **Status:** measured and attributed. The guest runs at PC=extended on 100% of
  operations, so the narrowing idea below is closed; the cost is the plumbing
  around the arithmetic, not the arithmetic.
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
