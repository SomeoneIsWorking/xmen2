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
