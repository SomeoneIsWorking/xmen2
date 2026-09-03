---
id: 141
title: engine=jit menu/in-game throughput is capped ~35 FPS by per-import guest<->host crossing cost
status: open
symptom: engine=jit steady-states at ~30-35 FPS at the menu with the frame cap removed; X2_HOTEP wall-time split charges ~70% of the interval to host-import crossings, ~28% to JIT guest bodies; QueryPerformanceCounter is called ~20k times/frame (3.6M per 5s interval), the top import by a wide margin, with _stricmp ~340k, LeaveCriticalSection/WaitForMultipleObjects/ReleaseSemaphore ~100k, floor/_CIfmod ~50k per interval
tags: jit,x86port,performance,crossings,pc,native
created: 2026-09-03
updated: 2026-09-03
---

## Where the time goes

Headless `--d3d8 --no-window --set engine=jit --unbounded`, run to steady
state at the attract-loop menu (scenes/presents ~26k), profiled with
`X2_HOTEP=64`:

```
wall-time split this interval: host imports ~3600 ms (72%), guest bodies ~1400 ms (28%)
import KERNEL32.dll!QueryPerformanceCounter: 3,601,309 call(s)   <- one interval
import MSVCR71.dll!_stricmp: 340,863 call(s)
import KERNEL32.dll!WaitForMultipleObjects: 92,973
import KERNEL32.dll!ReleaseSemaphore: 92,974
import KERNEL32.dll!LeaveCriticalSection: ~100-167k
import MSVCRT.dll!floor: 65,536
import MSVCRT.dll!_CIfmod: ~45k
```

~13-24M quantum preemptions/interval at a 20000-crossing quantum.

Caveats on the measurement: `X2_HOTEP` adds a `clock_gettime` pair per
crossing (`span_push`/`span_pop`), so the 72% figure is inflated by the
probe itself. An unprofiled run still steady-states ~35 FPS with host draw
+ upload at ~0.15 ms/frame, so the ~28 ms/frame that is not host render is
guest execution + crossings, and the crossing half of that is the lever.

## Root cause

Every guest->host import call makes `x86p_jit_engine_run` return
`kX86pRunIntercept`, unwinds the JIT slice, and `x2_engine_call` does the
callout (`x2_engine_callout_from/to_x86p` + `x86_dispatch` +
`x86_native_call_at` -> `thunk_call`) before re-entering
`x86p_jit_engine_run`. At ~20k QPC calls/frame that round trip dominates.
QPC is called that often because the game frame-limits / polls the clock in
a spin that the interpreter self-rate-limits and the JIT does not
(the same class as issue #57 / C207, a different spin site: the menu/frame
pacing loop, not libCriMovie).

## Options, roughly in leverage order

1. **x86port: inline intercept dispatch.** Add a consumer callback that
   x86port invokes from *inside* the run loop at an interception point,
   letting the consumer mutate `cpu` (regs/eip/esp) and continue the same
   slice, instead of unwinding `x86p_jit_engine_run` per thunk. Turns N
   thunk calls/slice from N run-function round trips into N direct calls.
   Needs design + tests in the x86port submodule; keep the current
   unwind path as the fallback for overrides/setjmp/return.
2. **Collapse the QPC spin like C207 collapsed the libCriMovie spin.**
   Identify the menu/pacing loop that polls QPC, prove its shape from the
   binary, and give it a bounded wait via a native override, A/B in the
   same binary. Biggest single-site win if the loop is as tight as the
   call count implies.
3. **Cheaper leaf-thunk fast path in `x2_engine_call`.** `_ftol` already
   has an inline path (90acdd0). The pure leaf thunks -- `_stricmp`,
   `floor`, `_CIfmod`, `QueryPerformanceCounter` (its only side effect,
   `winmm_timers_pump`, is callable inline) -- can be handled without the
   `x86_dispatch` + full callout, but doing it per-symbol duplicates each
   implementation; wants a shared "leaf handler" seam, not N `is_X_thunk`
   helpers.
4. **More native ownership** of the frame loop / crit sections so the hot
   guest code between presents is native and never crosses.

Rendering is already cheap at the menu (host draw ~0.1 ms/frame,
upload ~0.04 ms/frame); it is not the in-menu bottleneck. In-game (a real
level, not measurable headless without input scripting yet) will shift the
draw/skinning cost up but the crossing cost scales with it.
