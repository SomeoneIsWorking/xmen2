---
id: C085
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,ghidra,function-detection,coverage
---

## Claim

Vtable-derived seeding finds indirect-call targets in bulk, where the runtime finds them one per rebuild. 3,024 new functions in XMen2.exe from one pass, and the functions it creates are real: 99.9% start with a plausible prologue.

## Evidence

Measured 2026-08-06. tools/ghidra_scripts/SeedPointerTables.py scans read-only data for runs of >=3 consecutive aligned dwords pointing into an executable section -- what a vtable looks like and what stray constants do not. On XMen2.exe: 94,205 dwords scanned, 836 runs, 15,445 targets, of which 12,331 were already functions, 86 sit inside an existing function (they need a SPLIT, and are skipped rather than forced), 3,024 created, 4 failed. Function count 11,815 -> 14,840, instructions 647,015 -> 745,152. Quality check on the result: first-instruction histogram is PUSH 38.1%, MOV 33.7%, SUB 12.0%, FLD 5.0% -- 14,822 of 14,840 (99.9%) start with a plausible instruction. Of the 18 that do not, most are legitimate (DEC [ECX] refcount release, FILD float helpers, FEMMS for the 3DNow! path) and exactly 2 are spurious: one-instruction INT3 stubs created from padding, which translate to a named stop and are harmless.

## What would falsify it

if a seeded function is ever dispatched to and executes garbage rather than aborting, the >=3-consecutive-pointer heuristic is admitting non-vtable data and the threshold needs raising; and the 86 skipped 'inside an existing function' targets are NOT covered by this claim -- they remain missing until split
