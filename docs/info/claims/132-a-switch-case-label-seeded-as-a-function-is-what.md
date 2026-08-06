---
id: C132
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,rc-lift,ghidra
---

## Claim

A switch case label seeded as a FUNCTION is what leaves holes in a recreated container, and un-making it (not merging, not splitting) closes them

## Evidence

XMen2.exe 0x0066cf4e: --recreate wired all 10 entries of the table at 0x0066d645 and the body still had two holes (0x0066cf55..0x0066cfd3, 127 bytes; 0x0066cfdb..0x0066d042, 104 bytes), because entry 0 (0x0066cf79) was FUN_0066cf79 -- a single 'CMP dword ptr [EBP + 0x8],0x3' with no terminator. Ghidra will not absorb another function's entry point, so the flow walk stopped there and everything falling through from it stayed out. Deleting that function object and re-creating the container: 501 -> 577 instructions, 0 holes, verified by scanning the exported body for address gaps. The run then cleared 0x0066cf4e, which had stopped it, and reached IDirect3DDevice8::SetPixelShader. tools/ghidra_scripts/caselabel.py + tests/test_caselabel.py (ctest 'caselabel'), tools/ghidra_scripts/RecreateFunction.py

## What would falsify it

a case label with NO function on it that still leaves a hole after the table is wired -- that would mean the flow walk stops for a second reason this does not name. Also falsified if un-making one whose only references are computed jumps ever breaks a caller.
