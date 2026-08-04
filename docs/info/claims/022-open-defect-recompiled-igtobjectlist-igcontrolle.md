---
id: C022
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

OPEN DEFECT: recompiled igTObjectList<igController>::find and ::removeAllByValue FAULT where the original does not, reproducibly. This is a real translation bug, not a harness artefact, and it is the first one found.

## Evidence

difftest with three object shapes: find 10 BAD of 21 compared, removeAllByValue 11 BAD of 19, both reported as "FAULT in recompiled" -- the ORIGINAL completed and the recompiled code took an access violation on the same inputs. Both functions scan a list: load count from this+8, signed CMP/JGE early-exit, then LEA base+index*4 and a CMP/JZ ... CMP/JL loop. Both sides receive identical object contents and therefore identical list base pointers and counts, so a divergence in whether the scan is entered, or how long it runs, points at the signed-compare flags (SF/OF) or at the loop's flag state. Not yet root-caused. Both functions are EXCLUDED from the shipped DLL.

## What would falsify it

Root-causing it will either confirm a flag-model bug -- in which case every function using signed comparisons is suspect and the 156 verified results need re-examination for the same pattern -- or reveal another harness asymmetry. Until then, do not assume the lazy-flag model is correct just because 156 functions pass.
