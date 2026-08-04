---
id: C040
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The runtime-discovery loop works end to end on the Xbox target: seeding a function address found at runtime and re-lifting made the game's MAIN THREAD execute. It now runs, enters a critical section twice (kernel ordinal 277, RtlEnterCriticalSection) and returns.

## Evidence

tools.disasm --seed-functions with 0x0022286B (the PsCreateSystemThreadEx start routine, which the detector had never found because nothing references it statically) took the function count 21,907 -> 21,908, the re-lift kept 21,908/21,908 translated with 0 failures, and the address now appears in recomp_dispatch.c. The run output changed from 'start routine not found in dispatch' to 'PsCreateSystemThreadEx: main thread returned (g_eax=0x00000000)' with two ordinal-277 calls in between.

## What would falsify it

The main thread returns almost immediately with EAX=0 -- it does not enter a game loop, nothing renders, and no window is created. Two RtlEnterCriticalSection calls and an exit suggests it is bailing out early rather than running: the next step is to find why that routine returns, not to celebrate that it ran. 7,996 stubs remain unresolved.
