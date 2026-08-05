---
id: C047
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: vendor/xboxrecomp/tools/disasm
---

## Claim

The Xbox function detector under-sizes 6288 of 21909 functions: it ends a function at its last call target, leaving the tail -- including the __SEH_epilog path that restores ebx/esi/edi/esp/ebp -- in an unclaimed hole. Jumps into that tail become empty stubs, so the function returns with callee-saved registers clobbered.

## Evidence

7995 of the 7998 stubbed 'not detected' addresses fall in the hole between one detected function's end and the next function's start; only 3 are genuine mid-function entries. Measured on sub_00223DBD (0x223DBD-0x22416C, next function 0x2241E1): it tail-jumps to 0x2241D9, an empty stub, so __SEH_epilog is never called -- gdb shows ebx 0x1 -> 0x1000, edi 0x0 -> 0x00F80680, esp leaking 196 bytes across the call. The caller's subsequent signed compare against edi then takes the wrong branch and the title reboots.

## What would falsify it

if a hole belongs to a real but undetected separate function rather than the preceding one's tail, extending the boundary merges two functions
