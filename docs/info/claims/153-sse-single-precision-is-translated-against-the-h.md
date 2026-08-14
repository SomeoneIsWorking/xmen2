---
id: C153
kind: claim
status: holds
created: 2026-08-12
tags: recomp
reconfirmed: 2026-08-14
---

## Claim

SSE single precision is translated against the host CPU's own SSE, and the lane semantics -- not the arithmetic -- are what the test proves.

## Evidence

tests/test_sse.c: 30,101 comparisons against the real instructions over a corpus of NaNs, signed zeros, infinities and denormals, all 256 SHUFPS immediates, 0 failures; plus a discriminator that fails the suite if hardware MINPS and an fminf() model agree on that corpus. XMen2.exe unsupported instructions 4779 -> 2529, all remaining ones 3DNow!.

## What would falsify it

a guest that sets MXCSR (rounding mode or flush-to-zero) invalidates the 'host defaults match guest defaults' premise the intrinsics rest on -- only msdia80 contains LDMXCSR and it is not on any run path; if a run reports LDMXCSR being executed, this claim is void

## Re-confirmed 2026-08-14

Reconfirmed after GCC exposed operand swapping in the commutative MULPS intrinsic: ordered inline mulps now pins the architectural destination, and test_sse passes all 30,102 silicon comparisons including the six NaN-payload cases that failed before the fix.
