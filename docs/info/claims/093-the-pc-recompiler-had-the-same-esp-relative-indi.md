---
id: C093
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,translator,correctness
---

## Claim

The PC recompiler had the same esp-relative indirect-call defect that was fixed in the Xbox lifter four months of commits earlier

## Evidence

tools/recomp.py emitted an indirect CALL as: push the return address, THEN evaluate the memory operand. Real x86 computes the operand from the ESP the instruction was reached with and pushes afterwards, so every esp-relative target was read four bytes low. Gap::Core::igArkRegister is 'push edi; call dword ptr [esp+8]' -- the shape MSVC gives a callback passed as a stack argument -- so it dispatched to the caller's RETURN ADDRESS instead of the registration function it was handed, i.e. into the middle of arkRegister at +0xa. This is the SAME defect as commit 8a70f81, which fixed it in vendor/xboxrecomp on 2026-08-05 and even measured 40 esp-relative indirect-call sites there; the two translators are separate codebases and nothing re-checked this one. Fixed by snapshotting the target into a temporary before the push. Also fixed alongside: a CALL pushed its return address WITHOUT img_rel, so the value on the guest stack was a linked address -- 0x100177da rather than 0x240177da -- which resolved into whichever module occupies 0x10000000 and made the dispatcher report a missing body in the wrong module. VERIFIED: distinct (entry point, module) pairs entered rose 1398 -> 1548; the ARK dispatch failure is gone; battery 33/33.

## What would falsify it

A defect found in one lifter and not checked in the other is a process failure, not a one-off. If a third such shared-shape defect turns up in tools/recomp.py that vendor/xboxrecomp already fixed, then reading the Xbox fix log for applicable cases should become a standing step rather than something noticed by accident.
