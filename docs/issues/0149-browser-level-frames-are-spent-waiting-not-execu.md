---
id: 149
title: Browser level frames are spent waiting, not executing: WaitForSingleObject and SuspendThread cost 178-361 ms per call
status: investigating
symptom: browser level run presents 10-59 frames in 150 s; wall-time split attributes 100% of measured time to KERNEL32 waits, not guest bodies
tags: web,browser,wasm,threads,synchronization,performance
created: 2026-09-14
updated: 2026-09-14
---

## Symptom

A headless Chromium run of the packaged browser build (real 2.37 GiB install,
Dead Zone diagnostic boot, 150 s) presents 10-59 frames. With x86port's
hot-entry-point probe armed from the page (`?arg=--set&arg=hotep=4096`) the
heartbeat states where the measured time goes:

    [HB] top imports by TIME:
    [HB]   KERNEL32.dll!WaitForSingleObject: 3377.9 ms in 19 call(s)
    [HB]   KERNEL32.dll!SuspendThread:       1806.4 ms in 5 call(s)
    [HB]   KERNEL32.dll!QueryPerformanceCounter: 0.1 ms in 14 call(s)
    [HB] wall-time split this interval: host imports 5184.5 ms (100%),
          guest bodies 4.8 ms (0%)
    [HB] 145.2s crossings 289806 (+147)
          scenes 9 (+0)  clears 42 (+0)  draws 117 (+0)  presents 10 (+0)

That is 178 ms per `WaitForSingleObject` and 361 ms per `SuspendThread` while
the guest body time in the same window is 4.8 ms. The frame is not executing
slowly: it is waiting for a wake-up that arrives late, or never.

An unarmed run of the same route presents 59 frames in 150 s with
`perf: frame wall avg 2125.7 ms min 237.5 max 61997.3 (of 55 intervals) -- host
draw 0.39 ms/frame`, `JIT: 113258811 blocks entered, 1144722 translated
(5966096 instructions); 2038874 native hand-backs; 0 refusals`, `MAIN tid 999
... running guest code for 145.2s`, `0 preemption(s)`.

## Measured exclusions

* `requestAnimationFrame` in the same headless browser runs at 60.7 fps, so the
  page is not presentation-throttled.
* One wasm module per translated block is not the cost: `new WebAssembly.Module`
  plus `new WebAssembly.Instance` measured 3.2 us (empty), 4.2-4.7 us (154 B to
  4 KB), so 1.14M translations is about 5 s of a 140 s run.
* Import-call volume is small in a level interval (top by CALLS:
  `ReleaseSemaphore` 2689, `TlsGetValue` 2688, `WaitForMultipleObjects` 2688,
  `strstr` 1461), so this is not call volume.
* `host draw 0.39 ms/frame` over 9 draws per scene: not the renderer.
* The probe refuses keys past its cap and prints the refusal, so a small arm
  under-reports: an earlier 24-entry arm said "82% guest bodies" in a load
  window and admitted 16 refusals. 4096 was used here.

## Repro

    ./run.sh                                  # or the deployed page
    tools/x2ctl.py ...                        # local: see docs/web-release.md
    # browser: open the page with
    #   ?arg=--set&arg=hotep=4096
    # then Start Dead Zone gameplay test; the next heartbeat prints the split.

Negative control: the same route with no `?arg=` prints
`wall-time split: probe unarmed (X2_HOTEP)`.

## Next step

Find what those waits wait for, and whether the browser arm of the wake-up
(winmm timer fire, `PulseEvent`, the condition variable `kernel32_wait.c` parks
on, `SuspendThread`'s self-park in `threads.c`) arrives late or never. This is
the same class as issue #140, which the desktop path fixed with a winmm pump
point and per-thread call stacks.

### Note (2026-09-14)
Eliminated: the port's own pacing. The same route with pacing off (?arg=--unbounded&arg=--set&arg=unpaced=1) is WORSE, not better: 10 presents in 150 s, perf: frame wall avg 9428.9 ms min 471.9 max 63337.0 (of 8 intervals), winmm 62 fire(s) (+5) with 292877 pump(s) (+5). So the frame is not waiting on the port's frame limiter. What remains is the wait/pump cycle itself: the guest's winmm timer callbacks are pumped from inside guest wait calls (src/native/winmm.c has no host timer thread), the timer fires 1-5 times/s instead of 60, the main thread is inside a host import for 99% of the interval (WaitForSingleObject 178 ms/call, SuspendThread 361 ms/call), and the guest executes almost nothing (crossings +147 in a 5 s interval). Next instrument: report the wait loop's live state in the heartbeat -- what timeout it computed, which object it is waiting on, and whether the guest lock is held while it does -- since the current numbers cannot say which of those is late.

### Note (2026-09-14)
Two phases, measured separately. The browser stall is not one thing.

**Phase A -- asset load / boot: EXECUTION-bound, no blocking waits at all.**
The wait counters added for this issue (heartbeat line `wait sleeps N, asked X ms,
slept Y ms, worst oversleep Z ms`) report nothing in this phase, which means the
line's guard is honest: no wait blocked. In the same 45 s window:

    [HB] JIT: 33465787 blocks entered, 379506 translated
    [HB] winmm 0 fire(s) (+0), 629 pump(s) (+141), 0 timer(s) live
    [HB] 45.1s  crossings 113449 (+6846)
         scenes 1 (+0)  clears 3 (+0)  draws 1 (+0)  presents 1 (+0)
    [HB] ... and NO frame was presented in that time (still 1)
    [HB] MSVCR71.dll!_stricmp: 0.0 ms in 28515 call(s)

So the guest is running hard (0.74M block entries/s) and getting nowhere: one
scene, one draw, one present in 45 s, while parsing (`_stricmp` 28.5k calls,
`sscanf` 1118).

**The block rate is the number, and it is 20x off native.** Same counters, same
engine, same phase (boot), on this machine:

    native (scratch/touch-design/game.log, heartbeats 5.0 s apart):
      0 -> 77,710,687 blocks entered in 5 s      = 15.5M blocks/s
      77,710,687 -> 184,327,410 in 5.0 s         = 21.3M blocks/s
      184,327,410 -> 284,543,017 in 5.03 s       = 19.9M blocks/s
    browser (this issue): 33,465,787 blocks in 45.1 s = 0.74M blocks/s

That is ~21-27x, i.e. ~1.3 us per block entry against ~65 ns, with 0 refusals on
both sides -- the browser JIT translates and then *executes* slowly. Everything
else measured so far is rules out: one wasm module per translated block is ~5%
of the run (3.2-4.7 us per instantiate), import-call volume is small, `host draw`
is 0.39 ms/frame, and rAF runs at 60.7 fps in the same browser.

**Phase B -- menu/movie/idle: WAIT-bound** (the original symptom of this issue):
main thread inside a host import for 99% of the interval, `WaitForSingleObject`
178 ms/call, `SuspendThread` 361 ms/call, winmm firing 1-5 times/s against a
60 Hz expectation, guest executing almost nothing (`crossings +147` in 5 s).
Pacing is excluded: `--unbounded` with `unpaced=1` is worse, not better.

## Next

One probe decides the execution cost: measure a translated block's entry and
body cost in isolation, wasm against native. What that probe does NOT need to
look at, because it is already checked and wrong:

* **Per-entry JS bridging.** `x86p_jit_enter` casts the published entry to a
  function pointer and calls it (`src/x86port/jit_wasm.c`), and on wasm32 that
  index-into-the-table value compiles to a plain `call_indirect`. No JS runs per
  entry.
* **Optimization level.** The web tree configures `CMAKE_BUILD_TYPE=Release`
  (`-O3 -DNDEBUG`, checked in `build/web/CMakeCache.txt`), and the link has no
  `-sSAFE_HEAP`/`-sASSERTIONS=2`. `cmake/WebTarget.cmake` does carry
  `-sASYNCIFY=1` and `-pthread` with `ALLOW_MEMORY_GROWTH`, which are worth
  measuring but are a small multiple, not 21x.
* **Translation cost.** 379,506 translations in 45 s is 8.4k/s, and one
  instantiate measured 3.2-4.7 us.
* **Block size.** Both sides average 5.1 guest instructions per translated
  block (browser 379,506/1,930,219; native 850,639/4,307,070), so the browser is
  not dispatching smaller units.

So the cost is inside executing the emitted body or the engine's dispatch around
it, and `shared/x86port` owns both -- its Node/Emscripten test harness can time
them without a browser and against the native build. The number to reproduce is
1.3 us per block entry against 65 ns.

Both phases matter for playability; Phase A dominates loading, Phase B dominates
the menus.

### Note (2026-09-14)
The owner-side probe needed to explain the 21-27x has no working measurement either. x86port's tools/jit_bench.c, built for Emscripten and run under node, reports `jit 0.000 s` at 0.03 ns/insn and '10127.50x faster than the interpreter' (x86port d2136ac). Both are impossible: the JIT loop discards x86p_jit_enter's exit status, so a block that refuses immediately is timed as the fastest possible execution; the native-C column's only sink is an unreachable 0xDEADBEEF branch a wasm build can drop; and no engine's result is compared with any other's. It is now a distrusted instrument in x86port's ledger with the fix named (fail on any exit other than kX86pJitExitBlockEnd, sink every engine's result, require the engines to agree). So the next step is not a profile but repairing that bench: until it verifies its own work, no wasm-vs-native number may be quoted, and the only trustworthy measurement remains this issue's negative one -- 0.74M guest block entries/s in the browser against 15.5-21.3M natively for the same counters.

### Note (2026-09-14)
The owner's bench is repaired and it changes the picture. Three defects are gone from x86port/tools/jit_bench.c (d2136ac recorded them): the timed loop now refuses on any exit other than kX86pJitExitBlockEnd, the native columns run on the cache-aligned global the other columns use and fold into a printed sink, and one kernel is run through the interpreter and the JIT from the same seed and compared with x86p_cpu_diff before anything is timed. Natively it prints 'agreement: interpreter and jit leave identical state after one kernel' with plausible columns (native+flags 0.95 ns/insn, jit 0.39, interpreter 466.74). Under node it now REFUSES: 'the jit column stopped at unsupported instruction (exit 1) running the kernel once'. Two consequences. (1) Translation success is not a runnable block: the wasm translator covered all 64 kernel instructions and executing the result still exited unsupported, so this bench's kernel must be made one the wasm backend runs before any ratio is quoted. (2) The most worrying explanation of the 21-27x is dead, by check rather than argument: a block that cannot be JIT'd is a refusal in this product (x86_engine.c treats any status other than kX86pRunIntercept/kX86pRunBudget as refuse()), the gameplay binary links no interpreter at all, and the browser run reports 0 refusals of 379506 translation attempts. The browser is not interpreting; it is executing translated code 21-27x slower. Suspects that survive: the emitted body's efficiency under wasm and the engine's per-entry dispatch.
