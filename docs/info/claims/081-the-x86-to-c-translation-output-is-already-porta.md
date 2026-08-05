---
id: C081
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,native,sdl,portability
---

## Claim

The x86-to-C translation output is ALREADY portable: all 521 recompiled libIGDisplay bodies compile as native 64-bit Linux code with zero errors and zero warnings. Everything that binds the PC recomp to Wine lives in the runtime and interop layer, not in the emitted code.

## Evidence

Measured 2026-08-05: replaced x86rt.h's '#include <intrin.h>' with four stub FS/GS accessors (the ONLY thing it uses intrin.h for) and compiled the full src/recomp/libIGDisplay.c with the system gcc. 'gcc -c -O1' -> 0 errors, 0 warnings, producing a 160245-byte 'ELF 64-bit LSB relocatable, x86-64' object. Guest addresses survive because every PE base in play (0x400000 for XMen2.exe, 0x10000000 for the DLLs) is below 4 GB and can be mmap'd MAP_FIXED in a 64-bit process, so RD32/WR32's cast from uint32_t to a host pointer stays valid without -m32 (which is not installable here anyway: no 32-bit crt1.o).

## What would falsify it

if a native build that actually RUNS these bodies faults on a guest-address dereference, identity mapping is not sufficient and the memory accessors need a base-offset form; and if adding the x87/SBB/REP instructions currently unhandled (I005) introduces host-specific intrinsics, this 'zero errors' number no longer covers the whole translator
