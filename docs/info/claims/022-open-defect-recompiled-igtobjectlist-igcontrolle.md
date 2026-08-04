---
id: C022
kind: claim
status: falsified
created: 2026-08-04
tags: 
falsified_on: 2026-08-04
---

## Claim

OPEN DEFECT: recompiled igTObjectList<igController>::find and ::removeAllByValue FAULT where the original does not, reproducibly. This is a real translation bug, not a harness artefact, and it is the first one found.

## Evidence

difftest with three object shapes: find 10 BAD of 21 compared, removeAllByValue 11 BAD of 19, both reported as "FAULT in recompiled" -- the ORIGINAL completed and the recompiled code took an access violation on the same inputs. Both functions scan a list: load count from this+8, signed CMP/JGE early-exit, then LEA base+index*4 and a CMP/JZ ... CMP/JL loop. Both sides receive identical object contents and therefore identical list base pointers and counts, so a divergence in whether the scan is entered, or how long it runs, points at the signed-compare flags (SF/OF) or at the loop's flag state. Not yet root-caused. Both functions are EXCLUDED from the shipped DLL.

## What would falsify it

Root-causing it will either confirm a flag-model bug -- in which case every function using signed comparisons is suspect and the 156 verified results need re-examination for the same pattern -- or reveal another harness asymmetry. Until then, do not assume the lazy-flag model is correct just because 156 functions pass.

## FALSIFIED 2026-08-04

Overstated. A focused reproducer (tests/findrepro.c) builds the list object by hand and runs 54 combinations of count and start index -- including count=0, negative counts, negative starts and INT_MAX -- with ZERO mismatches. So find() is not wrong for any of those shapes, and calling it 'the first genuine translation defect' was not supported by the evidence I had. What is real and still unexplained is narrower: under the fuzzer, with a garbage list base pointer and a large random count, the recompiled scan takes an access violation on trials where the original does not. Both sides should read identical absolute addresses, so the likeliest explanation is another harness asymmetry -- most probably that the ORIGINAL is called first and something about that ordering, or the scan length, differs -- not a flag-model bug. The alarming corollary in the original claim (that all 156 verified results would be suspect) does NOT follow.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
