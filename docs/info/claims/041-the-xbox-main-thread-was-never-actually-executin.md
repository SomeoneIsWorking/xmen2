---
id: C041
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/seeds.json, xbox/src/recomp_types.h
---

## Claim

The Xbox main thread was never actually executing: sub_0022286B called its start routine (0x00225995) through RECOMP_ICALL_SAFE, which silently swallows an unresolvable target -- so 'main thread returned (g_eax=0)' was the macro's fake return, not the game's.

## Evidence

With the ICALL miss path instrumented, the run prints '[ICALL] UNRESOLVED VA 0x00225995 (icall #3) -- the call did NOT execute', and 0x00225995 is absent from recomp_dispatch.c. After seeding it in xbox/seeds.json and re-lifting (21908 -> 21909 functions, 0 failures), the same run executes 16 kernel calls -- file open, volume query, close, MmAllocateContiguousMemory -- from inside sub_00225995, confirmed by gdb backtrace.

## What would falsify it

if a run shows kernel calls originating below sub_00225995 in the backtrace WITHOUT the seed present, the ICALL was not the gate
