---
id: C021
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The x87 implementation is entirely UNVERIFIED: 0 of the 8 libIGDisplay functions containing x87 instructions are in the differentially verified set, and attempts to reach them failed.

## Evidence

Measured: 8 functions contain x87; 5 use indirect dispatch and 3 are test cases that land in the untestable bucket. Enabling dispatch functions in the fuzzer was tried and does NOT work -- the ORIGINAL recurses through a garbage vtable built from random object bytes and overflows the real stack, killing the run deterministically at igDefaultInterfaceManager::controllerSlider (reproduced twice, and a dispatch depth guard did not help because the recursion is inside the original's own code, not ours). Separately confirmed the earlier stated reason was wrong: libIGDisplay has ZERO float-returning exports, so ST(0) comparison was never the blocker.

## What would falsify it

Verifying x87 needs a different method than random-object fuzzing -- constructed valid objects, or extracting the arithmetic into a standalone comparison against the original's instruction sequence. Until then FILD/FMUL/FIDIV/FADD/FSUB/FCOMP/FNSTSW are written but unproven, and the 8 functions are excluded from the shipped DLL.
