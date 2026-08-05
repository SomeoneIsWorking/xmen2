---
id: C062
kind: claim
status: falsified
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
falsified_on: 2026-08-05
---

## Claim

The current blocker is an initialisation-ORDER problem, not corruption: the engine registry global at 0x0071037C is still NULL when sub_00268BD0 indexes through it, and with the ABI and stack contracts now verified clean across all 12,931 calls, nothing is overwriting it -- it was simply never set.

## Evidence

gdb at the fault: global 0x0071037C = 0x00000000, so ecx = 0, edx = MEM32(0) = the fake TIB word 0x00F7FF38, and esi = MEM32(edx + edi*4) with edi = 0x00F7FFEF (a stack address, not an index) produces the read of 0x81EC8BCD. Only three sites write that global -- sub_002742B0 sets it from sub_00269960's return, and two sites clear it. Only one thread is created in the whole run, so the synchronous PsCreateSystemThreadEx is not inverting any ordering.

## What would falsify it

if sub_002742B0 IS reached and its sub_00269960 call returns 0, the problem is that constructor failing rather than the order -- distinguish by breaking on sub_002742B0 itself, which the last attempt did not manage to place

## FALSIFIED 2026-08-05

Wrong framing, and it cost several rounds. The registry at 0x0071037C was not 'simply never set' by an ordering accident -- the code that would have set it was never reached because an allocation FAILED first, and it failed because our own NtAllocateVirtualMemory ignored a placed base (C070). Calling it an ordering problem sent the investigation looking for another caller of sub_002742B0, which does not exist. C065/C066/C067 remain accurate as measurements; only this diagnosis was wrong. Fixed by C071.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
