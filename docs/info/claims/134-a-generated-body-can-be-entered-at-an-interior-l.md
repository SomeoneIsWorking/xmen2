---
id: C134
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,rc-exe,translator
---

## Claim

A generated body can be entered at an interior label, so a shared MSVC epilogue needs no boundary surgery

## Evidence

MSVC shares one epilogue between paths, so a JMP lands in the middle of another function: 28 distinct targets in XMen2.exe (13 from 16 unconditional JMP sites, 17 from 22 conditional; 0 CALL sites), plus 1 each in libCriMovie and libIGGfx. The worked example is 0x0066cf3c -- PUSH ESI/PUSH EBX/CALL/ADD ESP,0xc/POP EDI/POP ESI/POP EBX/LEAVE/RET, inside FUN_0066ced2 which falls through into it, reached by JMP from the switch at 0x0066d633. It is NOT a boundary defect: FUN_0066ced2 ends in RET, so it is complete, and carving the block out would truncate its predecessor. CPU.enter_at carries the mapped resume address; the owner's prologue consumes and CLEARS it and jumps through the same offset switch its computed jumps use; recomp.py native registers one dispatch shim per interior target so an indirect dispatch resolves too. Verified on the real run: the --d3d8 --run path clears 0x0066cf3c and every other interior jump and now reaches IDirect3DTexture8::GetSurfaceLevel, deep in renderer resource work. 12 unit cases in tests/test_recomp.py; battery 36/36; ctest 11/11.

## What would falsify it

a direct CALL (not jump) to an interior address appearing in any module -- the mechanism as built does not push a return address and deliberately still reports those by name. Also falsified if a body is ever observed entering at a label with enter_at still set on a later ordinary call, which would mean the clear-on-consume is being bypassed.
