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

Before/after on the same XBE, one lift apart: [ABI] 22 violations across 7 distinct targets -> '94088 calls checked, every one restored ebx/esi/edi/ebp'. [STUB] '1 calls into empty stubs (of 320)' -> 'none of the 277 empty stubs was called'. The out-of-image indirect call to 0x00000000 from guest 0x002A975F is gone; boot advanced 511->525 kernel calls and 66968->67262 indirect calls. Logs scratch/logs/xbox_run_site.log (before) and scratch/logs/xbox_run_bounded.log (after).

The rule fires on 30 function bodies binary-wide and REFUSES 623 other branches past the clamp. Both numbers are printed every lift.

CORRECTION, same session: the first version of this rule had only the 'target is not a detected start' condition, reported 205 crossings, and was a RUNAWAY -- its own falsifier caught it one lift later. Measured by re-running the disasm stage with the crossing disabled: the true maximum function size in this binary is 8484 bytes, but with the unbounded rule eight functions passed 64 KB, a five-byte thunk at 0x00239910 grew to 872741 bytes and 0x003010C0 grew from 26 bytes to 878539. Once the walk steps into foreign code every branch THERE also looks like an interior branch and licenses the next crossing. Two further conditions bound it: at most ONE detected function may be swallowed (the ceiling is computed from next_func, never pulled along by a distant target), and at most one crossing per function. With those, the largest function is 8484 again -- identical to the crossing-disabled baseline -- and the runtime result is unchanged from the runaway build (same 67262 indirect calls, same 0 ABI violations, same next blocker), so every real benefit came from the 30 legitimate crossings.

Unit test vendor/xboxrecomp/tools/disasm/test_inner_func.py covers four classes: an interior branch crosses, a tail call does not, a branch two functions away does not, and crossings do not chain.

## What would falsify it

if a later lift shows a function body swallowing a genuine neighbouring function -- the rule refuses to cross onto a detected function START, so a tail call must never extend a body; a rising _crossed_inner_func count with new ABI violations would show the discriminator is wrong
