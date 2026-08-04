---
id: C028
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The recompiled XMen2.exe now runs continuously without hitting any untranslated instruction: it initialises D3D, keeps executing, and needs only 7 fallbacks to original code. It still renders nothing.

## Evidence

Added PUSHFD/POPFD with a real EFLAGS round-trip (new FK_EXPLICIT flag mode so the lazy model can be materialised and restored), CLC/STC/CMC/LAHF/SAHF, CPUID (reporting the REAL cpu, so the CRT's SSE routines are selected and run via fallback rather than being hidden behind a faked minimal feature set) and RDTSC. Run for 100s under Xvfb+DXVK: no x86_untranslated abort, no x86_dispatch miss, 7 distinct fallback addresses, process alive throughout. Earlier belief that it EXITED was wrong -- that was the wine explorer wrapper; timeout returns 124, i.e. still running.

## What would falsify it

Renders nothing: the captured frame has 1 distinct colour after 70 seconds. Whether it is looping in guest code or parked in a host call is STILL undiagnosed -- the watchdog thread added for exactly that question produced no output and has not been debugged. Running without aborting is not the same as running correctly.
