---
id: C281
kind: claim
status: holds
created: 2026-09-03
tags: pc,native,jit,performance,imports,leaf-thunks
depends: src/native/engine_leaf_thunks.c#engine_leaf_thunk_dispatch, src/native/x86_engine_dispatch.c#x86_engine_run_host_at
---

## Claim

Pure leaf import thunks (`_ftol`, `_stricmp`, `_strcmpi`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `toupper`, `tolower`, `strstr`, `TlsGetValue`) bypass the substrate register and x87 callout bridge via a direct leaf dispatch table on x86port CPU state, reducing in-game 2000-frame average wall time from 16.89 ms to 14.57 ms (-13.7%).

## Evidence

Telemetric analysis of in-game execution revealed millions of calls per heartbeat to simple leaf functions (`QueryPerformanceCounter`: ~2.6M calls; `_stricmp`: ~700k calls; `toupper`/`tolower`: ~195k calls; `TlsGetValue`: ~105k calls; `strstr`: ~83k calls). Each call previously triggered full register file copy, x87 stack serialization, hash-based thunk lookup in `x86_dispatch`, and full reverse register/x87 reconstruction.

`src/native/engine_leaf_thunks.{c,h}` introduces an $O(1)$ array-indexed fast path `(eip - THUNK_BASE) >> 4` that executes leaf functions directly on the `X86pCpu` struct. Pure leaf operations mutate return registers and stack pointers directly according to their calling convention (`__cdecl` vs `__stdcall`), preserving non-scratch registers and entirely skipping x87 translation.

Controlled by runtime CVar `engine.leaf_thunks` (default true) for instant A/B toggling without recompile. Tested in `tests/test_engine_leaf_thunks.c` covering each leaf function's stack cleanup, return value, parameter interpretation, and enable/disable toggle.

Measured in a 2000-frame headless in-game run (`act0/tutorial/tutorial1`, `X2_UNPACED=1`):
- Average frame wall time improved from 16.89 ms to 14.57 ms (-13.7%).
- Average present frame rate increased from ~59.2 FPS to ~68.6 FPS.
- Slow in-game frame count (25-40 ms bucket) decreased from 725 to 613 frames.

## What would falsify it

Any leaf thunk producing an incorrect return value, corrupted stack pointer, or mismatched behavior compared to the substrate bridge path (`--set engine.leaf_thunks=0`), or a regression in `ctest`.
