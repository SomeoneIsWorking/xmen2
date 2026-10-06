---
id: C290
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: vendor/shared/x86port/src/x86port/jit_x64.c#emit_x87_store_mem, vendor/shared/x86port/src/x86port/jit_x64.c#x87_store_mem_is_emittable
---

## Claim

x86port JIT emits x87 FST/FSTP-to-memory (m32/m64) natively; matches the interpreter including the ST(0)-empty-before-bounds-check order and RC-correct narrowing

## Evidence

jit.verify 246,362,572 in-game block entries agree 0 divergence (act0/tutorial 600 frames engine=jit); test_jit_x64 cases 62/96/97/98 with interpreter-vs-JIT full guest-memory memcmp; test_emit_x64 store64 Zydis oracle; x86port 19/19 xmen2 130/130; in-game x87 helper routing 8499->4227/boot

## What would falsify it

any jit.verify divergence on a block with FST/FSTP-to-memory, or a test_jit_x64 failure in the FST-mem cases
