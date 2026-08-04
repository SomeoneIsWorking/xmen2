---
id: C019
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The recompiler translates 100% of libIGDisplay.dll -- 521 of 521 functions, 8754 of 8754 instructions -- after adding ADC/SBB, REP string ops, MUL/IMUL/DIV/IDIV and the x87 subset the module actually uses.

## Evidence

tools/recomp.py report: 0 blockers. Coverage went 96.0% -> 98.5% (ADC/SBB + string ops) -> 99.8% (x87, once Ghidra's  operand form was parsed) -> 100.0% (MUL). The x87 model is a modelled register stack in long double (80-bit on x86, matching hardware internal precision) with stack under/overflow as hard faults. String ops assume DF=0, justified by MEASUREMENT: the module contains no STD or CLD at all.

## What would falsify it

TRANSLATION coverage, not correctness. Of the 521, only 156 are differentially verified; the x87 path is barely covered because float-returning functions come back in ST(0), which difftest does not compare at all -- it masks those returns to 0. A module containing STD would silently break the DF=0 assumption; that must become an error, not an assumption, before recompiling other modules.
