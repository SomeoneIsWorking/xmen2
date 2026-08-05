---
id: 8
title: A function containing another detected function was cut in half, and an interior branch became a tail call into an empty stub
status: resolved
symptom: Indirect call through a NULL function pointer (out-of-image 0x00000000) deep in engine setup, preceded by [ABI] 'did not restore ebx/ebp' on a chain of CRT functions and one [STUB] 'called 0x003DF55F, which was never detected as a function'
tags: xbox,recomp,function-boundaries,abi,empty-stub,crt
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

The boot died on `[ICALL] FATAL: out-of-image indirect call to 0x00000000`, at
guest 0x002A975F (`sub_002A9570+0x1EF`, `call dword ptr [edx+0x5c]`) -- a vtable
slot that was empty. The same run also printed 22 `[ABI]` callee-saved
violations across 7 targets and one `[STUB]` call into an undetected function.

## What made it findable

The ABI report named only the CALLEE, and the symbolized native stack could
only say `sub_002A9570 +0x1066` -- a compiled-C offset with no relation to any
guest offset. Adding the guest call-site VA to every emitted call (I018) turned
the seven scattered ABI lines into one nested chain:

    sub_002A9570+0x63 -> sub_00294900+0x145 -> sub_003D5E40+0xA
      -> sub_003D5DE4+0x35 -> sub_003DC43E+0x137 -> sub_003DF88F+0x28
        -> sub_003DF5F9+0x16F -> sub_003DF47D   <- innermost

Everything above the innermost frame was propagation. One root, not seven.

## Root cause

`sub_003DF47D` is MSVC's `_alloc_osfhnd`. It contains a nine-byte unlock
helper at 0x003DF556 that it ALSO `call`s directly, so 0x003DF556 is a genuine
call target *and* genuine interior code. `_find_function_end` clamps a
function's end at the next detected start, so `sub_003DF47D` ended at
0x003DF550 -- and its `je 0x003DF55F` at 0x003DF53F pointed past the clamp.
The boundary walk never followed it, so 0x003DF55F.. fell outside every
function.

The lifter then saw a branch leaving the function body and emitted a tail call:

    if (TEST_Z(MEM8(esi + 4), 1)) { sub_003DF55F(); return; }  /* je */

`sub_003DF55F` was an EMPTY STUB. The branch returned instead of running the
block, and ebx -- loaded at 0x003DF531 with `lea ebx,[esi+0xc]`, a critical
section address -- was never restored. Hence `ebx: 0x00000080 -> 0x01082134`.

## Fix

`_find_function_end` may now cross an intervening detected start, but only on
the evidence that separates interior code from a neighbour: a TAIL CALL jumps
to a function's FIRST instruction, an interior branch lands MID-BLOCK. It
extends only for a target that is not itself a detected start, and only as far
as the next detected start above it. 205 function bodies were extended.

## Result

    [ABI]  22 violations / 7 targets   ->  94088 calls checked, 0 violations
    [STUB] 1 call into empty stubs (of 320) -> none of 168 stubs called
    NULL indirect call                  ->  gone
    boot                                ->  511 -> 525 kernel calls

The next blocker is an ordinary discovery-loop input: `0x0029CA50 unresolved
(seed candidate)`.

## Dead end ruled out along the way

The worker-thread path in `bridge_PsCreateSystemThreadEx` saves/restores
`g_seh_ebp` rather than a `g_ebp`, which looks like a missed register. It is
NOT a bug: C051 collapsed the SEH bridge and `#define ebp g_seh_ebp` makes
`g_seh_ebp` the single storage for ebp. Do not 'fix' it.
