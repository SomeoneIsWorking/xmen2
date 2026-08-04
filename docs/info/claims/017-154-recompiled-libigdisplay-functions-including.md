---
id: C017
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

154 recompiled libIGDisplay functions -- including ones that call other recompiled functions and real libIGCore imports -- are verified against the original under forced relocation, and that set runs the game.

## Evidence

difftest widened to 392 candidate cases by resolving imports for real (x86_resolve_imports) instead of excluding call-ful functions: 154 verified, 6 failed, 232 untestable with random objects. The 154-function hybrid DLL runs the game; three frame samples gave 1609/303/2652 colours.

## What would falsify it

232 of 392 cases are UNTESTABLE by this method because random objects never yield valid input, so they are unverified rather than correct -- and they are excluded from the DLL. Trials are only 30 per case at this width, far fewer than the 4000 used for the narrow set.
