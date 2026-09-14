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

One probe decides the execution cost: time a translated block's entry path in
isolation, native against wasm. The prime suspect is per-entry JS bridging --
`jit_wasm_host.c` hands each translated block out through `addFunction(fn,'ii')`,
so if dispatch reaches it through Emscripten's `dynCall` the block crosses
wasm->JS->wasm on every entry, which is exactly the magnitude measured here.
`X86P_WASM_MAX_BODIES 64` already exists for batching modules ("what the block
cache will want") and the shipping path does not use it.

Both phases matter for playability; Phase A dominates loading, Phase B dominates
the menus.
