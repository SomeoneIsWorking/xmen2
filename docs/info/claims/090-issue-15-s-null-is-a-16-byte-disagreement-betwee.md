---
id: C090
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,memory,rc-exe
---

## Claim

Issue #15's NULL is a 16-byte disagreement between ms->top and the arena's actual top chunk

## Evidence

Fault is exactly guest 0x10057f27 MOV EDX,[EDI] with EDI=0, confirmed by a -O0 build of the chunk (I025) agreeing with -O2, plus the register dump (I024): eax 0 ecx 28 edx c ebx 0 esi 00a80004 edi 0. Tracing consolidate by hand against peeked arena bytes (I022): the chunk at esi=0x00a80004 has head 0x203, whose size field decodes ((h>>5)&0x7ffff, rounded to 4, plus (h&0x1e)*2, plus 4) to a 24-byte span, so the walk lands on 0x00a8001c. At 0x10057efb consolidate compares that against ms->top and takes a merge path if equal -- but ms->top (malloc state 0x71002328, field +0x2c) reads 0x00a8002c, 16 bytes higher. So the branch is not taken, consolidate treats the top chunk as an ordinary free chunk and unlinks it, reading fd from [chunk+12] = [0x00a80028] = 0. Arena dump supports this: 0x00a8001c holds 0x80000180 which decodes with its +8 word (0x94000027) to size 0x138000C, the full 19.5 MB arena, i.e. it IS the top chunk; while 0x00a8002c, where ms->top points, holds a head of 0x00000000, which is not a valid chunk at all. The same high-size word 0x94000027 appears at both 0x00a80024 and 0x00a80034, 16 bytes apart.

## What would falsify it

This is a hand-trace of bit-packed header arithmetic against a memory dump, NOT a differential comparison against the original DLL, so it establishes WHERE the two disagree and not WHICH side is wrong. If the original produces the same ms->top and the same 0x203 header, then the defect is in neither and consolidate is being called in a state the stock build never reaches. tests/difftest.c comparing igArenaInitState and the malloc path against the shipped libIGCore.dll is what would decide it.
