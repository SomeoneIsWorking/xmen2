---
id: C065
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

Corrected picture of the blocker: sub_002902C0 IS reached with the right arguments (this=0x01083440, its vtable 0x004C5220, slot 0xA0 = 0x002902C0, gating counter 0x00710384 = 0), but it returns before reaching its own late call to the registry initialiser sub_002742B0 at loc_00290557. So the registry at 0x0071037C stays NULL.

## Evidence

gdb stopping at sub_002902C0 confirms entry (frame 0 printed, ecx=0x01083440); the next stop is the SIGSEGV in sub_00268BD0, so the breakpoint on sub_002742B0 never fires. The constructor at sub_002AC4F0:41448 returns the object correctly and the virtual call at :41465 dispatches to the right slot. Inside sub_002902C0 the registration gate passes -- counter 0, this non-null -- so the early exits at loc_002902F4 are not the reason.

## What would falsify it

my earlier gdb scripts printed their message whichever stop occurred, which is how C063 came to claim the function was never reached; any conclusion here that does not name the frame it was read from is suspect for the same reason
