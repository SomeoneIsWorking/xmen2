---
id: C281
kind: claim
status: holds
created: 2026-09-03
tags: pc,native,jit,performance,imports,fastpath
depends: src/native/x86_import_fastpath.c#x86_import_fastpath_dispatch, src/native/x86_engine_dispatch.c#x86_engine_run_host_at
---

## Claim

Eligible native imports (`_ftol`, `_stricmp`, `_strcmpi`,
`QueryPerformanceCounter`, `QueryPerformanceFrequency`, `toupper`, `tolower`,
`strstr`, `TlsGetValue`) dispatch directly against the canonical x86port CPU
state instead of taking the general host-import route, reducing in-game
2000-frame average wall time from 16.89 ms to 14.57 ms (-13.7%).

## Evidence

Telemetric analysis of in-game execution revealed millions of calls per
heartbeat to simple imported functions (`QueryPerformanceCounter`: ~2.6M calls;
`_stricmp`: ~700k calls; `toupper`/`tolower`: ~195k calls; `TlsGetValue`: ~105k
calls; `strstr`: ~83k calls). The general import path performed routing and
host-boundary work that these fixed, side-effect-bounded operations do not need.

`src/native/x86_import_fastpath.{c,h}` introduces an $O(1)$ array-indexed
fast path `(eip - THUNK_BASE) >> 4` that executes eligible imports directly on
the `X86pCpu` struct. These operations mutate return registers and stack
pointers directly according to their calling convention (`__cdecl` versus
`__stdcall`) while preserving non-scratch registers.

Controlled by runtime CVar `engine.import_fastpath` (default true) for instant A/B
toggling without rebuilding. Tested in `tests/test_x86_import_fastpath.c`
covering each eligible import's stack cleanup, return value, parameter
interpretation, and enable/disable toggle.

Measured in a 2000-frame headless in-game run (`act0/tutorial/tutorial1`, `X2_UNPACED=1`):
- Average frame wall time improved from 16.89 ms to 14.57 ms (-13.7%).
- Average present frame rate increased from ~59.2 FPS to ~68.6 FPS.
- Slow in-game frame count (25-40 ms bucket) decreased from 725 to 613 frames.

## What would falsify it

Any import fast-path producing an incorrect return value, corrupted stack pointer, or
mismatched behavior compared to the ordinary host-import path
(`--set engine.import_fastpath=0`), or a regression in `ctest`.
