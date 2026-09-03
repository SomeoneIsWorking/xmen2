---
id: 140
title: The guest x86 JIT engine renders black after the legal splash -- translated through interception points, monopolised the guest lock, shared the engine call stack across threads
status: resolved
symptom: With engine=jit (the default) the screen is black after the legal splash; the guest spins at ~550 FPS submitting one textured quad per frame while the libCriMovie decoder thread sits SUSPENDED. engine=interpreter renders the menu.
tags: jit,x86port,threads,movie,deadlock,pc,native
created: 2026-09-03
updated: 2026-09-03
---

## Symptom

`775712c` ("enable guest x86-64 JIT execution") landed on a "played the game"
claim. It never rendered this title. Under `engine=jit`:

* legal splash text never draws;
* the guest warms the JIT (~15 s) then spins forever: `scenes == draws ==
  presents`, one textured fullscreen quad per frame, ~550 FPS, framebuffer
  `mean_luma 0.00`;
* the three libCriMovie threads (`0x25002590`, `0x25002600`, `0x25002630`) are
  parked -- two in a condition wait, the decoder SUSPENDED -- for the whole run;
* `MAIN tid 999 ... running guest code for 117.3s`, `0 preemption(s)`.

`engine=interpreter` reaches the menu (`mean_luma ~62`), slowly (~9 FPS).

## Three distinct causes, each with its measurement

### 1. The JIT translated basic blocks straight through consumer interception points

x86port's dispatch checks the consumer's `jit_intercept` predicate only
*between* translated blocks. A host thunk, an `ENGINE_RETURN_ADDR`, or a native
override entry reached by **fall-through** (not by a branch) sat in the middle
of a block, so the JIT ran the raw guest bytes there instead of handing control
to the host. The interpreter checks every instruction and was unaffected.

Fix, in x86port (`75c7a08`): `x86p_jit_translate_bounded` +
`x86p_jit_engine_set_boundary`. xmen2's `jit_boundary` (the pure-EIP subset of
`jit_intercept`: thunk range, `ENGINE_RETURN_ADDR`, `x86_native_body_at`,
`x86_setjmp3_thunk`) stops a block before any interception point.

Measured: guest logic went from "stuck at 1 present" to progressing; a
94,429,816-entry whole-machine + memory cross-check of every JIT block entry
against a shadow interpreter (x86port's new per-entry verify,
`x86p_jit_engine_set_verify`) found **zero divergences**. x86port per-block
execution is correct.

### 2. A thread executing JITted code never yielded the guest lock

A recompiled body reaches the preemption point in `X86_ENTER_FN`
(`src/recomp/x86rt.h`) on every call, and that is what issue #57's movie
rendezvous depends on. JITted guest code carries no such point, so a thread
that stayed inside JITted code -- MAIN, in a libCriMovie playback loop polling
the decoder -- held the single guest lock (`threads.c` `g_lock`) for its whole
duration. The decoder's feeder threads could never run to resume it. "0
preemptions while MAIN runs guest code for 117 s" is that, exactly.

Fix (`src/native/x86_engine.c`): run the JIT in slices bounded by
`guest_quantum_size()` and call `guest_quantum()` between them -- the same
quantum the recompiled path uses. It is a no-op when no other guest thread is
blocked on the lock.

### 3. The engine's call-frame stack was process-global, not per-thread

`x2_engine_call` is re-entered by every guest thread that reaches
non-recompiled code. Its frame stack (`g_frame[]`) and depth counter were
`static`, guarded only by the comment "exactly one guest thread is inside this
loop at a time". Cause 2's fix broke that assumption: once MAIN yields
mid-call, the decoder thread enters `x2_engine_call` too, and the shared depth
counter makes `jit_intercept`'s `eip == frame->return_to` check read the wrong
frame. A libCriMovie thread then ran past its own `0xDEADBEEF` entry sentinel
(pushed by `x86_guest_call_args`) into unmapped memory:

    *** SIGSEGV at 0xdeadbeef (not an import slot) -- address not mapped
    [ENGINE]   0x25002ea0 (unnamed) at 0xdeadbeef, esp 71a01d70

Fix: `src/native/x86_engine_frames.{c,h}` -- the frame stack and its depth are
`__thread`; `deepest` stays a cross-thread high-water for the report only. This
also took `x86_engine.c` back under the 500-line cap.

## Result

`engine=jit` now plays the intro FMV reel correctly (Activision logo, then the
X-Men Legends II opening cinematic -- proper colours, pillarboxed), decoder
thread running, MAIN yielding on ~0.0 s intervals, no `0xdeadbeef` crash. The
movie phase runs slower than the interpreter's (~8-12 FPS vs ~21) -- a
performance follow-up, not this issue.

**Follow-up, measured 2026-09-03:** the movie phase is *not* yield-cadence
bound. A full headless movie-reel run reported `0 preemption(s)` for its entire
duration -- the libCriMovie decoder is scheduled through
`__wrap_fn_libCriMovie_10002520`'s `guest_cond_wait_ms`, not through guest-lock
contention, so `guest_quantum()` between JIT slices never fires there. Coarsening
the JIT slice from the 200k-instruction cap to 1<<20 changed nothing
(~8-12 FPS either way). Each movie frame is ~170 ms of pure guest logic; the
cost is x86port executing libCriMovie's decode/colour-convert loop, which the
interpreter happens to run faster per-frame here. A real fix needs profiling
that loop under the JIT (block-entry churn vs a hot arithmetic/SIMD path
x86port lowers poorly), not scheduler tuning. Menu renders correctly at
~21-43 FPS; movie renders correctly (Marvel/Raven/Activision logos + opening
cinematic, proper colours, pillarboxed). Not blocking.

## Regression coverage

* x86port `tests/test_jit_engine.c`:
  `test_boundary_ends_a_block_before_a_flagged_address`,
  `test_verify_reports_an_in_block_self_modification`.
* xmen2 `tests/test_engine_frames.c`: two pthreads keep independent frame
  stacks (aborts if the stack is made shared again), plus the single-thread
  push/pop/depth-restore contract.
* A full headless boot-to-menu parity run is an observation, not a gate
  ([[game-playing-runs-are-observation-not-gates]]): verified once by hand here
  (both engines reach the menu, `engine=jit` 1.06B block entries, 0 fallbacks,
  0 refusals, no crash).

## Do not

Do not "fix" the slow movie phase by removing the quantum yield -- that is what
unsticks the rendezvous. Do not make the engine frame stack shared again. Do
not tune the JIT slice size for the movie phase: it was measured to have no
effect (0 preemptions there); the slowness is JIT execution cost in the
libCriMovie decode loop, addressable only by profiling that loop.
