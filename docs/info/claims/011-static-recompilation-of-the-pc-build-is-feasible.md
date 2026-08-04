---
id: C011
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

Static recompilation of the PC build is feasible at the decoder level: Ghidra's recursive descent identifies function bodies covering ~78-80% of executable bytes in BOTH the symbol-rich DLLs and the symbol-free XMen2.exe, and a decoder handling ~80 x86 mnemonics covers 99.7% of the exe's instructions.

## Evidence

InstrHisto.py over Ghidra function bodies (NOT a linear sweep). XMen2.exe: 11,106 functions, 2,025,452 of 2,613,248 exec bytes (77.5%), 643,647 instructions, 186 distinct mnemonics, top-50 = 98.76%, top-80 = 99.71%. libIGDisplay.dll: 521 functions, 26,220 of 32,768 bytes (80.0%), 8,754 instructions, only 54 mnemonics, top-50 = 99.95%. Integer core dominates (MOV/PUSH/CALL/LEA/POP/TEST/ADD/JZ/CMP/JNZ = 80% of the exe); x87 is 41 mnemonics at 5.83%; SSE is 0.18%.

## What would falsify it

The 22.5% of exe bytes in NO identified function is the unmeasured part -- if a large share of it turns out to be reachable code rather than data/padding/alignment, boundary discovery becomes the bottleneck again. Also untested: whether the identified boundaries are CORRECT, not merely present. And instruction COUNT coverage is not semantic coverage -- one mis-modelled flag or x87 precision case breaks execution regardless of histogram share.
