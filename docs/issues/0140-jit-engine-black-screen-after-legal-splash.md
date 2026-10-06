---
id: 140
title: The guest x86 JIT engine renders black after the legal splash -- translated through interception points, monopolised the guest lock, shared the engine call stack across threads
status: resolved
symptom: With the JIT selected, the screen is black after the legal splash; the guest spins at ~550 FPS submitting one textured quad per frame while the libCriMovie decoder thread sits SUSPENDED. The then-available diagnostic interpreter mode renders the menu.
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

The then-available diagnostic interpreter mode reaches the menu (`mean_luma
~62`), slowly (~9 FPS). That comparison isolated correctness; it is not product
performance evidence.

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

A native boundary reaches the preemption point in `X86_ENTER_FN`
(`src/runtime/x86_abi/x86rt.h`) on every call. JITted guest code carries no
such point, so a thread
that stayed inside JITted code -- MAIN, in a libCriMovie playback loop polling
the decoder -- held the single guest lock (`threads.c` `g_lock`) for its whole
duration. The decoder's feeder threads could never run to resume it. "0
preemptions while MAIN runs guest code for 117 s" is that, exactly.

Fix (`src/native/x86_engine.c`): run the JIT in slices bounded by
`guest_quantum_size()` and call `guest_quantum()` between them. It is a no-op
when no other guest thread is
blocked on the lock.

### 3. The engine's call-frame stack was process-global, not per-thread

`x2_engine_call` is re-entered by every guest thread. Its frame stack
(`g_frame[]`) and depth counter were
`static`, guarded only by the comment "exactly one guest thread is inside this
loop at a time". Cause 2's fix broke that assumption: once MAIN yields
mid-call, the decoder thread enters `x2_engine_call` too, and the shared depth
counter makes `jit_intercept`'s `eip == frame->return_to` check read the wrong
frame. A libCriMovie thread then ran past its own `0xDEADBEEF` entry sentinel
(pushed by `x86_guest_call_args`) into unmapped memory:

    *** SIGSEGV at 0xdeadbeef (not an import slot) -- address not mapped
    [ENGINE]   0x25002ea0 (unnamed) at 0xdeadbeef, esp 71a01d70

Fix: `src/native/x86_guest_call_stack.{c,h}` -- each live host call owns an
intrusive frame in a thread-local stack; `deepest` stays a cross-thread
high-water for the report only. This also took `x86_engine.c` back under the
500-line cap.

## Result

The JIT now plays the intro FMV reel correctly (Activision logo, then the
X-Men Legends II opening cinematic -- proper colours, pillarboxed), decoder
thread running, MAIN yielding on ~0.0 s intervals, no `0xdeadbeef` crash. The
movie phase ran slower than the diagnostic interpreter control (~8-12 FPS vs
~21). That timing helped locate the path but is not conformance evidence.

### 4. Deep nesting lost the current hand-back frame

The first JIT integration kept two call-stack representations: an unbounded
host context stack and a fixed 64-entry diagnostic array. Deep boot nesting
made the array's current-frame lookup return null, which suppressed native
override hand-back and sent the JIT through the original guest bodies.

The current `src/native/x86_guest_call_stack.{c,h}` is one intrusive,
thread-local stack whose nodes live in `x2_engine_call` host frames. Intercept
predicates, inline import dispatch, longjmp restoration, fault reporting, and
nesting telemetry all read that same stack; there is no fixed-depth shadow
state to diverge. The hand-back address predicate also remains unconditional.

## Regression coverage

* x86port `tests/test_jit_engine.c`:
  `test_boundary_ends_a_block_before_a_flagged_address`,
  `test_verify_reports_an_in_block_self_modification`.
* xmen2 `tests/test_x86_guest_call_stack.c`: two pthreads keep independent
  stacks; the single-thread case covers push/pop, deep nesting, and longjmp
  restoration without a shadow copy.
* xmen2 `tests/test_jit_intercept.c` (cause 4): uses 69 live intrusive frames
  and asserts both current-frame retention and native-override hand-back; it
  also covers no-frame, shallow-frame, thunk, plain-guest,
  selftest-in-place, and translation-boundary cases.
* A full headless boot-to-menu parity run is an observation, not a gate
  ([[game-playing-runs-are-observation-not-gates]]): verified once by hand here
  (the JIT and then-available diagnostic interpreter both reached the menu; the
  JIT recorded 1.06B block entries, 0 fallbacks, 0 refusals, and no crash).

The old `engine=interpreter` selector described in this issue was diagnostic
only and is not a supported gameplay mode. Current product policy selects the
JIT unconditionally; only a bounded, counted fallback after failed/unsupported
compilation or unsafe emitted execution is permitted, and any fallback-backed
interval is excluded from gameplay and performance evidence.

## Do not

Do not remove the quantum yield -- that is what unsticks the movie rendezvous.
Do not make the guest-call stack shared again or add another call-context
stack beside it. Do not gate the
thunk/return-addr/native-body checks in `x86_engine_jit_intercept` /
`x86_engine_jit_boundary` on `x86_guest_call_top()` returning non-NULL. The
earlier note here claimed the movie phase was
"JIT execution cost in the libCriMovie decode loop" -- that was wrong; the intro
was decoding through the guest's MMX kernel because cause 4 suppressed the
native FMV override. With the fix the reel plays through native FFmpeg.
