---
id: C063
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

The registry initialiser is never REACHED, not failing: breakpoints on both write sites of 0x0071037C never fire before the fault. The chain is sub_002902C0 -> sub_00290560's call site -> sub_002742B0, and nothing calls sub_002902C0 statically.

## Evidence

gdb with breakpoints on recomp_0013.c:78061 and :78068 (the two writes to 0x0071037C) reaches the SIGSEGV without either firing. Static xrefs give exactly one caller of sub_002742B0, at 0x00290560, which lives in sub_002902C0; that function has no static callers at all. The indirect-call tally is clean (0 unresolved of 7648), so if it were reached through a function pointer it would have run -- which points at an earlier branch never taking the path that leads there.

## What would falsify it

if sub_002902C0 is reached through a pointer stored in memory that a working run would populate, the missing step is that store, not a branch
