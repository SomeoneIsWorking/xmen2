---
id: C100
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,discovery,tooling
reconfirmed: 2026-08-06
---

## Claim

Callback addresses passed as code immediates were invisible to every discovery pass, and there are over a thousand of them

## Evidence

The native discovery loop learns ONE function per round -- the runtime stops at the first dispatch target it cannot resolve, and each round costs a Ghidra re-analysis, a re-emit and a relink. On libIGGfx it found exactly one per round for eight rounds and hit its cap still going. Inspecting the shipped bytes at those addresses showed why: each is the operand of a PUSH imm32 immediately followed by a CALL through an import -- a callback handed to a registrar. Nothing branches to them, so no reference-driven analysis creates a function, and SeedPointerTables cannot see them either because it scans read-only DATA for runs of pointers, not the instruction stream. tools/seed_code_imms.py scans the instruction stream instead and found 1138 across the modules in one pass (988 in XMen2.exe alone). Wired into native_discover.sh as a bulk pass before the per-round loop. MEASURED after seeding and rebuilding: distinct (entry point, module) pairs entered 2819 -> 2974, battery 33/33, ctest 5/5, and the run advanced past the missing-function wall to a C++ operator new import.

## What would falsify it

Only PUSH and MOV immediates are scanned. A callback address materialised any other way -- computed, loaded from data into a register and then passed, or built by arithmetic -- is still invisible, and the run stopping on another missing dispatch target (it does, at libIGGfx 0x1006ea90) is evidence that this pattern is not the only one.

## Re-confirmed 2026-08-06

Extended and re-measured. The first version skipped any instruction containing a bracket, which was too blunt: the engine also builds callback tables with , where the brackets are the DESTINATION and the immediate is exactly the wanted function pointer -- that was the very target the run stopped on after the first pass. Reading the SOURCE operand alone keeps those and still rejects , where the same number is a load address. That found a further 669 function starts: 476 more in libIGGfx, 168 in XMen2.exe, 23 in libIGSg, 2 in libIGCore. RESULT: tools/native_discover.sh now CONVERGES ON ROUND ONE -- its bulk pass runs first and the per-round loop then reports no missing targets at all, where before it found one per round until it hit its cap. Distinct (entry point, module) pairs entered 2974 -> 3733.
