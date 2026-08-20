---
id: C186
kind: claim
status: holds
created: 2026-08-14
tags: native,recomp,runtime,abi
reconfirmed: 2026-08-21
verified_at: 2026-08-21 02:12:32
depends: src/native/x86_tail_policy.c#x86_tail_route
---

## Claim

Freshly emitted native code now has the same depth-aware indirect tail-dispatch contract as the hosted recompiler runtime

## Evidence

Re-emitting XMen2 after isolating 0x006281f0 made many generated TAIL_DISPATCH calls visible and the native link failed because x86_tail_dispatch was absent. src/native/x86rt_native.c now implements the C181 contract: same-frame tails iterate, tails from a deeper direct generated call dispatch before that caller resumes. All 22 XMen2 translation units link. A real default capped run maps 16453 exe bodies and reaches 18 presents, 36 draws, 0 refused before the explicit 10-frame stop.

## What would falsify it

A nested native generated tail call resumes its direct caller before the target finishes, or a full native run shows guest ESP imbalance attributable to the dispatch loop

## Re-confirmed 2026-08-21

2026-08-21: x86_tail_route is now the shipping runtime's single tail-depth decision and ctest x86_tail_policy exercises both discriminating cases: dispatch_depth=1/call_depth=0 queues, while dispatch_depth=1/call_depth=1 runs inline. The former reversed equality fails both expectations. A Clang rebuild linked x2native with the production policy and the focused test passed.
