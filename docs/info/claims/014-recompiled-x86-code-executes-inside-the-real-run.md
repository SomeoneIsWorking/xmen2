---
id: C014
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

Recompiled x86 code executes inside the real running game: a hybrid libIGDisplay.dll with 82 recompiled functions and 745 exports forwarded to the original loads and runs the game through its intro cinematic.

## Evidence

tools/recomp.py dll emits export shims and import stubs that switch ESP between the host C stack and the guest stack and let the REAL callee do its own argument cleanup, then read ESP back -- so no argument counts are needed in either direction. Export name set matches the original exactly (898/898). tools/run_shim.sh recomp -> game_image_loaded=yes, running at 60s, frame shows the letterboxed intro cinematic.

## What would falsify it

Only 82 of 748 exported code entry points are recompiled; the rest are forwarded, so this is mostly the original DLL still. A visually-correct frame does NOT prove the 82 are behaving correctly in-process -- see C014. Growing the recompiled set is expected to break this and will need bisection: an all-500 build crashed with a page fault.
