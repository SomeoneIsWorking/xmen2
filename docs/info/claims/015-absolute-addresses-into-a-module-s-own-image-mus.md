---
id: C015
kind: claim
status: holds
created: 2026-08-04
tags: 
reconfirmed: 2026-08-04
---

## Claim

Absolute addresses into a module's own image MUST be emitted relative to its runtime base, not its preferred base: the original libIGDisplay.dll gets 0x10000000 in a small process but is RELOCATED to 0x001C0000 inside the game, where every hardcoded 0x100xxxxx reference reads unrelated, still-mapped memory -- silently.

## Evidence

WINEDEBUG=+loaddll shows libIGDisplay_orig.dll at 0x10000000 in the difftest process and at 0x001C0000 in the game. The emitted C contained 358 hardcoded absolute image references. Fixed by emitting them as (G_IMGBASE + RVA) with g_imgbase resolved from the actual module handle at load; 801 references now rebased, and the only hardcoded addresses left are pushed return addresses that are never dereferenced. difftest still 82/82 after the change.

## What would falsify it

This makes the earlier verification's validity conditional in a way that was NOT noticed at the time: C013's 292,700 clean trials happened to be run in a process where the DLL got its preferred base, so they did not test the relocated case at all. A difftest that FORCES relocation is still owed -- until then the rebasing fix is reasoned and compiled, not differentially verified.

## Re-confirmed 2026-08-04

Now differentially verified, not just reasoned. difftest reserves 0x10000000 before LoadLibrary, forcing relocation (observed at 0x00c20000), and all 82 functions still match over 292,692 trials. NEGATIVE CONTROL fires exactly: pinning g_imgbase to the preferred base while the DLL is relocated (X2_BREAK_REBASE=1) produces 20 FAILED -- precisely the 20 of 82 functions independently measured to contain absolute image references. Predicted and observed failure counts match.
