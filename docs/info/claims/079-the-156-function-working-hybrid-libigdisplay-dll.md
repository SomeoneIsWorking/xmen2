---
id: C079
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,libigdisplay,verification,instrument
---

## Claim

The 156-function 'working' hybrid libIGDisplay.dll passes because NONE of its recompiled functions ever executes on the intro path. Zero recompiled entry points are entered in a 25-second run that renders the intro. So that build demonstrates the proxy/forwarding machinery works, NOT that the recompiled code runs correctly in-process -- and it explains why growing the set breaks immediately: the moment recompiled code actually runs, it faults.

## Evidence

src/x86watch.c X2_WATCH=all (I019) traces every recompiled entry point. Good build (156 eps): the instrument initialises -- '[WATCH] watching ALL entry points, global cap 40' is in scratch/run/watch/x2watch.log -- and NOT ONE ENTER line follows in 25s, on a run that rendered the intro (game_image_loaded=yes wrapper_alive=yes, 3459 distinct colours). An independent run without the selftest produced no log file at all, which is the same result by a different route. POSITIVE CONTROL, on the same instrument and the same day: the failing build (good + 0x10002c00) DOES produce ENTER lines, so silence is a measurement and not a dead hook. The exit hook is present in the generated C at 585 sites against 521 enter sites.

## What would falsify it

if a longer run, a different scene (past the intro movie), or a different entry-point set produces ENTER lines from the 156-function set, then those functions DO run and this claim is too strong -- the honest scope is 'not on the intro path in 25 seconds'
