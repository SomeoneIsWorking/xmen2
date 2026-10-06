---
id: C287
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: vendor/shared/x86port/src/x86port/jit_x64.c#emit_x87_load, vendor/shared/x86port/src/x86port/jit_x64.c#x87_load_is_emittable, vendor/shared/x86port/src/x86port/emit_x64.c#x86p_emit_x87_m
---

## Claim

x86port JIT emits FLD natively (32/64-bit float from memory and FLD ST(i)) instead of interpreter-helper routing; behaviour-equivalent

## Evidence

jit.verify 246,775,239 in-game block entries agree 0 divergence; test_jit_x64 generator cases 61/66/89 differential vs interpreter; test_emit_x64 decodes the new x87 primitives with Zydis; 19/19 x86port, 130/130 xmen2

## What would falsify it

any jit.verify divergence on a block containing FLD, or a test_jit_x64 failure in cases 61/66/89, or a test_emit_x64 x87-form failure
