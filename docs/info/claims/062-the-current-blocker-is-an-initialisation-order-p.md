---
id: C062
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

The current blocker is an initialisation-ORDER problem, not corruption: the engine registry global at 0x0071037C is still NULL when sub_00268BD0 indexes through it, and with the ABI and stack contracts now verified clean across all 12,931 calls, nothing is overwriting it -- it was simply never set.

## Evidence

gdb at the fault: global 0x0071037C = 0x00000000, so ecx = 0, edx = MEM32(0) = the fake TIB word 0x00F7FF38, and esi = MEM32(edx + edi*4) with edi = 0x00F7FFEF (a stack address, not an index) produces the read of 0x81EC8BCD. Only three sites write that global -- sub_002742B0 sets it from sub_00269960's return, and two sites clear it. Only one thread is created in the whole run, so the synchronous PsCreateSystemThreadEx is not inverting any ordering.

## What would falsify it

if sub_002742B0 IS reached and its sub_00269960 call returns 0, the problem is that constructor failing rather than the order -- distinguish by breaking on sub_002742B0 itself, which the last attempt did not manage to place
