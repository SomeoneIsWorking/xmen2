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

CORRECTION (C079): this claim's own falsifier turned out to be the right worry. 'Recompiled x86 code executes inside the real running game' is not established by a visually-correct frame, and measurement now says the opposite on the intro path: with X2_WATCH=all (I019) and a positive control, ZERO recompiled entry points are entered in a 25-second run of the 156-function successor to this build. The interop layer is what was demonstrated, not the execution of the recompiled bodies.

## What would falsify it

Only 82 of 748 exported code entry points are recompiled; the rest are forwarded, so this is mostly the original DLL still. A visually-correct frame does NOT prove the 82 are behaving correctly in-process -- see C014. Growing the recompiled set is expected to break this and will need bisection: an all-500 build crashed with a page fault.
