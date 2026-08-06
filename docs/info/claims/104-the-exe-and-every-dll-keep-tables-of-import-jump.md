---
id: C104
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,discovery,tooling
---

## Claim

The exe and every DLL keep tables of import jump thunks that Ghidra does not make functions

## Evidence

MSVC emits a six-byte  per imported function and calls the import through it. Nothing contains those thunks and they are reached indirectly, so Ghidra often leaves them outside any function and the recompiler has no body -- the native discovery loop was finding them one at a time, four rounds each landing six bytes from the last, all inside one table at 0x00643f00 in the exe. Counted across the thirteen recompiled modules: 208 in XMen2.exe, 240 in libIGGfx, 172 in libIGOpt, 153 in libIGSg, 152 in libIGGui, 141 in libIGMath, 85 in libIGAttrs, 79 in libIGUtils, 58 in libIGDisplay, 36 in libCriMovie, 30 in libIGLua, 29 in libIGCore, 25 in libMovie -- 1408 in total. Seeding them removed the dispatch-target wall entirely: distinct (entry point, module) pairs entered 3840 -> 4161, and the run now stops on a host import (MSVCRT!sscanf) rather than a missing body. Battery 33/33.

## What would falsify it

Most of the 1408 were ALREADY functions in Ghidra's database -- XMen2.exe gained only 11 functions from 208 thunks, and libMovie and libCriMovie gained none. So the class is real and the seeding is cheap, but the number that MATTERED is small and unmeasured: nobody has counted how many of the 1408 were newly created rather than already covered.
