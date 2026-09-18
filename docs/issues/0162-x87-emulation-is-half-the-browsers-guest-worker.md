# 0162 — x87 emulation is half the browser's guest worker

- **State items:** S021
- **Status:** measured; no cause analysis yet beyond "there is no 80-bit float
  in WebAssembly"
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

`x86p_x87_software_narrow` at 3.46% says a narrowing path already exists; what
it does and when it is taken has not been read yet, and that is the first thing
to find out rather than guess at.

## The question worth asking first

x87 has a precision-control field in its control word, and Win32 sets it to 53
bits — double — at process start. A game compiled in 2005 computing `float`
and `double` math through x87 does not need, and by its own control word does
not ask for, 80-bit intermediate results.

So the question is whether this run's guest is executing with PC=53 and paying
for 80-bit arithmetic it has told the FPU not to produce. If it is, the work is
to compute in the host's `f64` while the control word says 53 bits, and fall
back to the softfloat only when the guest actually asks for extended precision.
That is a correctness-preserving change: the guest's own control word is the
authority for what the result must be.

If instead the guest sets PC=64, the 80-bit path is required and the work is a
faster one, not a narrower one.

**This is a question, not a plan.** Nothing here has read the guest's control
word, and a change built on the assumption that PC=53 without measuring it
would be exactly the kind of "make the numbers look better" fix this project
refuses.

## What would falsify the attribution

The profile is one 15-second window of one route with translation quiescent. If
a window taken during combat, or on another map, puts x87 well below 47%, then
this is the cost of one scene's math and not of the game's. Worth taking a
second window before building anything.
