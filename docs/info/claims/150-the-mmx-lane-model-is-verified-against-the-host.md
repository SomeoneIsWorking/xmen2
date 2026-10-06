---
id: C150
kind: claim
status: holds
created: 2026-08-11
tags: translator,simd
---

## Claim

The MMX lane model is verified against the host CPU's own SSE2, not against a second reading of the manual

## Evidence

tests/test_mmx.c runs every implemented MMX op over six operand pairs chosen for the cases that separate a correct model from a plausible one (negatives, values that saturate both ways, a high bit in every lane, zero) and compares against the equivalent SSE2 intrinsic on the same bits -- 715 checks, 0 failures. Where the 128-bit form is not a drop-in the operands are rearranged so the low 64 bits are exactly the MMX result: the HIGH interleaves take bytes 8-15 rather than 4-7 (so the top halves are fed in as low halves), and the packs take all eight lanes of their first operand (so destination and source go in one register's two halves). Shift counts are tested AT and PAST the lane width (0,1,7,15,16,31,32,63,64,200), which is where C's own >> is undefined and where MPEG code with a variable count actually goes. The test carries its own discriminator: an arithmetic and a logical shift of a negative vector must DIFFER, or the harness could not tell a wrong model from a right one.

## What would falsify it

a decoded frame that differs from the same frame under Wine, or any check in tests/test_mmx.c failing after a change to the lane helpers. The test covers only the 64-bit MMX forms -- the 128-bit SSE forms and all of 3DNow! (PFMUL/PFADD/PFRCP, which libIGGfx uses on the AMD path) are NOT implemented and NOT covered.
