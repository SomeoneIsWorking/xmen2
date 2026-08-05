---
id: C048
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0003-recompiler-jump-tables-and-loud-drops.patch
---

## Claim

Two boundary defects in the Xbox function detector accounted for 7995 of 7998 silently-empty stubs: _find_function_end swept linearly and stopped at the first terminator (so unconditional jumps into a function's tail never extended it), and when it did extend, it set the end TO the jump target, excluding the instructions at that target.

## Evidence

Replacing the linear sweep with a worklist that follows both conditional and unconditional intra-function jumps and DECODES each target takes the stub count 7998 -> 4923 -> 348 and the generated C from 1.53M to 1.96M lines, with 21,909/21,909 still translating and 0 jumps deleted. In the run, the title stops rebooting: it goes from 20 kernel calls to 200+, creates its TDATA/UDATA save directories under title id 41560047, and crashes in its own engine code instead.

## What would falsify it

a hole that is a genuinely separate undetected function, not the preceding function's tail, would be merged --  is clamped to the next DETECTED function, so this can only merge across an undetected boundary
