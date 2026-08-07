---
id: C141
kind: claim
status: holds
created: 2026-08-07
tags: recomp,native,rc-exe,translator
---

## Claim

recomp.py assumed a function's ENTRY POINT is the lowest address in its body, so 12 functions across 3 modules ran from the wrong instruction. MSVC places an adjustor thunk far from the code it jumps to and Ghidra merges the ranges into one function whose entry is the HIGHER address.

## Evidence

XMen2.exe FUN_005d4d80 is ADD ECX,0x867ac / JMP 0x005bee90 at 0x005d4d80, with the code it jumps to at 0x005bee90. The emitted body fell into 0x005bee90 and put the ADD after a return, so the array base was never applied: (0x710d9c0c - 0x71053460) / 804 = 685 instead of (0x710d9c0c - 0x710d9c0c) / 804 = 0, and the run faulted reading element 685 of a 175-element array in FUN_005beca0 (issue #36). The count is a scan of every module JSON for ins[0].a != ep: 7 in XMen2, 4 in libCriMovie, 1 in libIGGfx, all 12 with an instruction AT the ep. After emitting 'goto L_<ep>' the fault is gone and the run stops on a named missing feature, IDirect3DDevice8::CreateStateBlock.

## What would falsify it

a generated body whose first executable statement is neither its entry point's label nor a goto to it (grep the chunks for a function where ins[0].a != ep and no 'goto L_<ep>' follows the prologue)
