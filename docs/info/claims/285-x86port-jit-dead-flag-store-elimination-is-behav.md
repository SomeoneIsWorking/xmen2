---
id: C285
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: vendor/shared/x86port/src/x86port/jit_x64.c#flag_write_is_dead, vendor/shared/x86port/src/x86port/jit_x64.c#emit_alu_inline, vendor/shared/x86port/src/x86port/cpu_compare.c#diff_flags
---

## Claim

x86port JIT dead flag-store elimination is behaviour-equivalent to storing every flag tuple

## Evidence

jit.verify shadow-interpreter cross-check over 211,006,759 in-game block entries (X-Men Legends II act0/tutorial/tutorial1, 401 frames) agreed with 0 divergence; x86port test suite 19/19 incl test_jit_x64 randomized differential; jit_bench kernel 2279->808 host bytes

## What would falsify it

any jit.verify divergence in a 'lazy flags' or derived CF/PF/AF/ZF/SF/OF field on a block that hit flag_write_is_dead; or a guest fault/exception handler observing stale EFLAGS after an elided store
