---
id: C131
kind: claim
status: holds
created: 2026-08-06
tags: recomp,translator,defect
---

## Claim

PUSH and POP of an ESP-relative operand were translated one dword wrong, and it was live in the shipped path

## Evidence

Intel: PUSH r/m32 computes a memory operand's effective address from the ORIGINAL ESP, and POP r/m32 computes its destination address AFTER the increment. recomp.py emitted 'C->esp -= 4; WR32(C->esp, RD32(C->esp + 8));' -- a single statement in which the read uses the already-decremented ESP, so it read [esp+4]. Only operands BASED ON ESP can tell the difference, which is why it survived: every register push is identical either way. FOUND ON REAL DATA, by dumping the guest stack at a failing malloc rather than by reading code: XMen2.exe 0x0065e314 is a four-instruction allocator whose entire body is 'PUSH dword ptr [ESP+8]; CALL malloc', and the stack showed +04 (what was pushed) holding a caller's pointer 0x700ff678 while +16 held the real size 0x50 that 0x0065b85d had pushed. Fixed by reading the source into a temporary before moving ESP, and by moving ESP before computing POP's destination; three unit tests cover it including PUSH ESP. Every module was re-emitted. VERIFIED BY THE RUN GETTING FURTHER: the malloc failure, the guest's out-of-memory path and the crash in its uninstalled handler are all gone, and the run now stops somewhere else entirely.

## What would falsify it

The fix is verified by one failure disappearing, not by a differential trace against the original. If another ESP-relative stack defect exists it would look the same. The honest check would be a run compared instruction-for-instruction against the Wine oracle, which does not exist yet.
