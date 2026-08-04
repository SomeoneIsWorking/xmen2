---
id: C013
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

Recompiled x86 code is VERIFIED CORRECT against the original binary, not merely compiling: 82 recompiled functions match the shipped libIGDisplay.dll exactly over 292,700 randomised trials, 0 mismatches.

## Evidence

tests/difftest.c, built 32-bit and run under Wine. Each trial fills a 512-byte object with deterministic PRNG bytes, calls the ORIGINAL via GetProcAddress and the recompiled C with the same object/arg/EAX, and compares EAX. Covers exported, translated, call-free, write-free functions -- exercising MOV/TEST/Jcc/SHR/SHL/AND/OR/XOR/ADD/SUB/LEA and the lazy-flag model. NEGATIVE CONTROL PASSED: two independent injected bugs (AND EDX,0xf->0x7 and SHL EDX,8->9) were each detected as exactly 1 failing case.

## What would falsify it

Scope is narrow and must not be over-read: only call-free, write-free functions, only EAX compared (not memory writes, not other registers, not the flag state on exit), and x87/SBB/REP-string functions are excluded entirely because they do not translate yet. Extending to functions with memory writes or calls could expose defects this cannot see.
