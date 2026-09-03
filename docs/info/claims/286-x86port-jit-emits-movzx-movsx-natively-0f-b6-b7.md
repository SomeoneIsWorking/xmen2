---
id: C286
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: vendor/shared/x86port/src/x86port/jit_x64.c#emit_movx, vendor/shared/x86port/src/x86port/jit_x64.c#movx_is_emittable, vendor/shared/x86port/src/x86port/emit_x64.c#x86p_emit_sar_r32_imm8
---

## Claim

x86port JIT emits MOVZX/MOVSX natively (0F B6/B7/BE/BF, reg and mem sources) instead of interpreter-helper routing; behaviour-equivalent

## Evidence

jit.verify 140,422,308 in-game block entries agree 0 divergence; x86port test_jit_x64 generator cases 85-88 differential vs interpreter; 19/19 x86port, 130/130 xmen2

## What would falsify it

any jit.verify divergence on a block containing MOVZX/MOVSX, or a test_jit_x64 failure in cases 58/59/85-88
