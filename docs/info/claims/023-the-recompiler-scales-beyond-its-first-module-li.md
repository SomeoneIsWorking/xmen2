---
id: C023
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The recompiler scales beyond its first module: libIGDisplay, libIGAudio and libIGCollision translate at 100%, and XMen2.exe -- the game's main executable, with no symbols at all -- translates at 99.6% of functions (11,061 of 11,106) and 98.8% of instructions, emitting 1,738,619 lines of C that compile to a 25MB object.

## Evidence

tools/recomp.py report/emit on each module. XMen2.exe went 90.7% -> 99.1% -> 99.6% as three blocker classes were closed: FS-segment access (825 functions -- MSVC's SEH prologue, handled by reading the real TIB since we execute as a genuine 32-bit PE), the x87 control word plus MMX/SHLD/byte-IMUL, and routing the 4 remaining unresolved call targets through x86_call_unknown. Remaining blockers are 45 functions using MMX/3DNow! (PXOR/PFMUL/PAND/PUNPCKHDQ) and CPUID -- the CRT's feature-detected memcpy paths.

## What would falsify it

COMPILES, NOT RUNS. Not one instruction of recompiled XMen2.exe has executed; there is no entry shim, no import layer for its 989 imports, and no differential test for any of it -- the 156 verified functions are all still in libIGDisplay. The x87 additions (FSIN/FCOS/FYL2X/FPATAN/control-word rounding) and the MMX model are entirely unverified, and MMX is modelled as separate registers although on hardware it ALIASES the x87 stack -- safe only while no module interleaves them without EMMS, which has not been checked.
