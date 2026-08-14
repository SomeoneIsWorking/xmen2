---
id: 70
title: MULPS intrinsic let the compiler swap the architectural destination and changed NaN payloads
status: resolved
symptom: test_sse reports six MULPS failures where numeric results agree but the quiet-NaN payload comes from the opposite operand
tags: pc,recomp,sse,translator,compiler
created: 2026-08-14
updated: 2026-08-14
---

## Cause

`_mm_mul_ps` is mathematically commutative, so GCC swapped its operands. x86 MULPS is not bit-commutative for NaN payload propagation: the encoded destination determines which payload survives. The runtime therefore differed from a real `mulps src,dst` in six of 30,102 differential cases.

## Fix and evidence

`src/recomp/x86rt.h` now emits ordered inline `mulps` with the destination as a read/write operand. `ctest -R ^sse$` passes all 30,102 comparisons against silicon. The test was not weakened.
