---
id: C289
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: vendor/shared/x86port/src/x86port/x87.c#x86p_x87_to_f32, vendor/shared/x86port/src/x86port/x87.c#host_narrow
---

## Claim

x86port x87 FST/FSTP-to-memory narrows by the guest control word's RC field (x86p_x87_to_f32/f64), not always to nearest

## Evidence

test_fst_rounds_by_the_control_word: hand-computed nearest-even/up/down/truncate anchors on a half-ulp value + routing sweep vs host FPU (3072 stores, 0 mismatch); jit.verify 211,934,992 in-game block entries agree; x86port 19/19 xmen2 130/130

## What would falsify it

test_fst_rounds_by_the_control_word failure, or a host-FPU vs x86p_x87_to_f32 mismatch under any RC mode
