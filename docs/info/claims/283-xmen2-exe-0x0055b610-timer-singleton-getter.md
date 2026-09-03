---
id: C283
kind: claim
status: holds
created: 2026-09-04
tags: pc,native,jit,performance,startup,timer
depends: src/native/startup.c#x2_override_0055b610
---

## Claim

Bypassing `x86_guest_body` in `x2_override_0055b610` (`XMen2.exe!0x0055b610`) once the timer singleton is initialized directly returns the instance pointer (`0x007ac248`) and pops `ret` (`C->esp += 4u`), eliminating ~2.8M guest SEH frame constructions per 2000 in-game frames and reducing 2000-frame wall time from 31.95s to 30.95s (present framerate rising to 64.7 FPS).

## Evidence

In a 2000-frame in-game profiling run (`act0/tutorial/tutorial1`), `XMen2.exe!0x0055b610` was entered 2,804,213 times (accounting for 5.6M JIT block transitions). Disassembly of `0x0055b610` showed an MSVC Meyers singleton: on every invocation, it constructed a full SEH exception frame (`fs:[0]`), tested byte `0x007ac288` bit 0, and if already initialized, tore down the SEH frame, loaded `eax = 0x007ac248`, and executed `ret`.

Because `x2_override_0055b610` previously invoked `x86_guest_body` on every single query, each of the ~2.8M calls crossed back into the engine to execute the guest SEH setup/teardown.

In `src/native/startup.c`, `x2_override_0055b610` was updated to check whether the initialization guard byte `0x007ac288` has bit 0 set. On the very first call, it defers to `x86_guest_body` so retail initialization and atexit registration execute unaltered. On all subsequent calls, it sets `C->eax = s_inst_addr`, pops the return address (`C->esp += 4u`), and returns directly to the caller.

Measured in a 2000-frame unpaced in-game benchmark (`act0/tutorial/tutorial1`, `X2_UNPACED=1`):
- Average frame time reduced from 15.14 ms to 14.65 ms.
- Average present framerate increased from 62.7 FPS to 64.7 FPS (+3.2%).
- Total wall time for 2000 frames decreased from 31.95s to 30.95s (-1.0s).

## What would falsify it

The timer singleton address returning a stale or invalid pointer before initialization, stack imbalance upon return, or a regression in any test suite or in-game pacing.
