---
id: C086
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,translator,boundaries,esp
---

## Claim

A function body that ends without a terminator must be emitted as an explicit FALL-THROUGH to the next address, not left to fall off the end of the generated C. 53 functions across the project were in that state.

## Evidence

Measured 2026-08-06. tools/verify_export.py counts bodies whose last instruction is not RET/JMP/INT3: XMen2 42 of 14929, libIGGfx 4, libIGCore 1. The failure mode is silent and distant: FUN_00554ba0 (a 308-instruction SEH-using constructor) is clamped before the next detected start, so its emitted C returned with two un-popped pushes and an un-torn-down SEH frame; its caller's RET then popped a saved frame pointer, and the run died four frames and thousands of instructions later at an address in the guest stack. Two fixes, both measured: MergeTruncated.py absorbs a spurious inner function and re-creates the outer one (42 -> 7 in XMen2 over three passes, refusing to delete inner functions with real callers -- it correctly declined __CxxFrameHandler with 610 callers), and the emitter now emits a tail call to the next address when a body has no terminator, which is what the hardware does.

## What would falsify it

if a fall-through is ever emitted to an address that is not a function, x86_fallthrough aborts naming both -- that firing means the boundary is wrong in a way merging did not fix; and if verify_export ever reports 0 truncated while a run still dies on a stack-shaped RET, the diagnosis was incomplete
