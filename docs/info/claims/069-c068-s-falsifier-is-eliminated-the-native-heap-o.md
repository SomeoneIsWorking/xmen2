---
id: C069
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp_manual.c
---

## Claim

C068's falsifier is eliminated: the native heap override is not what makes the factory's allocation fail. With XBOX_NATIVE_HEAP=0 the run gets FEWER indirect calls (7629 vs 7648) and dies on the older C056 heap fault at 0xCCCCCCCC instead, so the override is strictly better and the allocation failure is upstream of it.

## Evidence

XBOX_NATIVE_HEAP=0: 476 kernel calls, 7629 indirect calls, '[NHEAP] native heap DISABLED', fault at Xbox VA 0xCCCCCCCC. With the override: 471 kernel calls, 7648 indirect calls, fault at 0x81EC8BCD via the registry path. Same binary, one environment variable.

## What would falsify it

both runs die, so 'strictly better' is measured in calls reached, not in correctness -- if the override changes WHICH allocation the factory makes, the comparison is not like-for-like
