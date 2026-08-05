---
id: C077
kind: claim
status: holds
created: 2026-08-05
tags: xbox,recomp,function-boundaries,abi
---

## Claim

A detected function can sit INSIDE another function's body, and clamping the outer function's end at 'the next detected start' silently cut it in half. MSVC's _alloc_osfhnd (0x003DF47D) contains a 9-byte unlock helper at 0x003DF556 that it also calls directly, so the helper is a real call target AND real interior code. The outer function's 'je 0x003DF55F' pointed past the clamp, the boundary walk did not follow it, and everything from 0x003DF55F on fell outside every function. The recompiler then emitted that in-function branch as 'sub_003DF55F(); return;' -- a tail call into an EMPTY STUB -- so the branch silently returned instead of running the block, and ebx (holding a critical-section address) was never restored. That corruption propagated up seven frames and killed the boot on a NULL vtable slot 30 calls later.

## Evidence

Before/after on the same XBE, one lift apart: [ABI] 22 violations across 7 distinct targets -> '94088 calls checked, every one restored ebx/esi/edi/ebp'. [STUB] '1 calls into empty stubs (of 320)' -> 'none of the 168 empty stubs was called'. The out-of-image indirect call to 0x00000000 from guest 0x002A975F is gone; boot advanced 511->525 kernel calls and 66968->67262 indirect calls. Logs scratch/logs/xbox_run_site.log (before) and scratch/logs/xbox_run_bounds.log (after). The rule fired on 205 function bodies binary-wide, not just this one. Unit test vendor/xboxrecomp/tools/disasm/test_inner_func.py covers both directions.

## What would falsify it

if a later lift shows a function body swallowing a genuine neighbouring function -- the rule refuses to cross onto a detected function START, so a tail call must never extend a body; a rising _crossed_inner_func count with new ABI violations would show the discriminator is wrong
