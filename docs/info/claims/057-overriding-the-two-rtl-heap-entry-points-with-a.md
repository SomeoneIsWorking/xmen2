---
id: C057
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp_manual.c, xbox/CMakeLists.txt
---

## Claim

Overriding the two Rtl heap entry points with a native allocator gets the title past the CRT entirely and into its own engine code: 476 -> 531 kernel calls, 4077 -> 4157 indirect calls, and the 0xCCCCCCCC heap fault is gone.

## Evidence

RtlAllocateHeap (0x002241E1, stdcall ret 12) and RtlFreeHeap (0x00222433, stdcall ret 12) are wrapped at link time with -Wl,--wrap, so the recompiled bodies stay linked as __real_ and XBOX_NATIVE_HEAP=0 runs them instead. recomp_lookup_manual was tried first and reported '0 allocations -- nothing called RtlAllocateHeap', because it only covers INDIRECT calls and these are direct. With the arena active at 0x01081000..0x02881000 the run reaches sub_0026BB80 -> sub_0026C050 -> sub_0028C4F0 -> sub_0028A820 -> sub_00289F90 -> sub_0026C410 -> sub_0026E740, all game .text, and stops on a virtual call through an object pointer holding 0x00225995 -- a code address.

## What would falsify it

RtlReAllocateHeap and RtlSizeHeap are NOT overridden. If the CRT's realloc reaches the recompiled heap with a pointer from the native arena, it will read the native block header as its own and corrupt it -- and nothing currently detects that
