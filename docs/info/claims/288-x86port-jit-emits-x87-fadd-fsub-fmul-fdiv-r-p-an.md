---
id: C288
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: vendor/shared/x86port/src/x86port/jit_x64.c#emit_x87_arith, vendor/shared/x86port/src/x86port/jit_x64.c#emit_x87_store_reg, vendor/shared/x86port/src/x86port/jit_x64.c#x87_arith_is_emittable
---

## Claim

x86port JIT emits x87 FADD/FSUB/FMUL/FDIV (+R/+P) and FST/FSTP ST(i) natively instead of interpreter-helper routing; behaviour-equivalent

## Evidence

jit.verify 248,040,303 in-game block entries agree 0 divergence (act0/tutorial 600 frames engine=jit); test_jit_x64 differential cases 63/64/67/70/90-95 vs interpreter; x86port 19/19, xmen2 130/130; in-game x87 helper routing 14930->8499 translations/boot

## What would falsify it

any jit.verify divergence on a block with an x87 arith or FST/FSTP-reg op, or a test_jit_x64 failure in the x87 cases
