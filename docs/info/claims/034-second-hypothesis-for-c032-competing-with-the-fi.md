---
id: C034
kind: claim
status: falsified
created: 2026-08-05
tags: 
falsified_on: 2026-08-14
---

## Claim

Second hypothesis for C032, competing with the field-offset one: the presentation parameters are passed as a struct BY VALUE and our emitted stack adjustment is off, so the engine reads the structure shifted rather than reading wrong values.

## Evidence

Width=1 and Height=43253760 are not merely wrong, they are inconsistent with each other -- a plain field-offset error reading alchemy.ini's [Viewer] block would produce recognisable neighbouring values, and 43253760 (0x02942A00) matches no field there. A shifted structure explains arbitrary-looking values in every member at once, including the format and depth-format changing together. Four recompiled functions contain both 0x320 (800) and 0x258 (600) and are the starting candidates: fn_00514980, fn_005160e0, fn_0049a8f0, fn_00426a50.

## What would falsify it

Both hypotheses are still speculation about a value pattern. Settle it by tracing what the exe actually passes -- gen_trace.py already builds boundary tracers, so trace the engine calls of the ORIGINAL exe and of the recompiled exe and diff the argument sequences. That is the same compare-do-not-guess move that found the defect in the first place.

## FALSIFIED 2026-08-14

The shifted-by-value-structure hypothesis depended on C032 being a current defect. C180 shows the current Wine path passes the exact stock structure, so this hypothesis predicts a mismatch that is absent; it never established the historical cause.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
