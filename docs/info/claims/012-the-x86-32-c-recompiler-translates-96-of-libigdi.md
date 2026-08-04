---
id: C012
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The x86-32 -> C recompiler translates 96% of libIGDisplay.dll's functions (500 of 521, 80.4% of instructions) and the emitted C compiles clean with -Wall under i686-w64-mingw32.

## Evidence

tools/recomp.py report/emit over Ghidra-exported boundaries: 500/521 functions, 7042/8754 instructions. 20,349 lines of C compile to a 154KB object with zero errors and zero warnings. Every remaining blocker is a named ordinary instruction class, not a structural problem: FILD/FLD (x87, 8 fns), SBB (5), and REP string ops STOSD/MOVSD/CMPSD/SCASB (8).

## What would falsify it

COMPILING IS NOT CORRECT. Nothing emitted has been EXECUTED -- not one recompiled function has run, and no output has been compared against the original. Lazy-flag semantics, x87, and the calling-convention/interop layer are all unexercised. The first real test is running a recompiled function and diffing its effects against the original; expect this number to mean much less than it looks like until then.
