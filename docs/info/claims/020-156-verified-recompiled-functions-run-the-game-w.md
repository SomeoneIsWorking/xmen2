---
id: C020
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

156 verified recompiled functions run the game with 100%-translated code available; frame samples show real rendering.

## Evidence

difftest at 394 cases: 156 verified, 1 failed (igTObjectList::find, 1 of 18 trials -- unexplained, and excluded), 237 untestable with random objects. The 156-function hybrid renders; samples 1609/301/2658 colours.

## What would falsify it

The single igTObjectList::find failure is NOT understood -- 1 bad trial in 18 could be a real translation bug in a rarely-hit path or another harness artefact. It is excluded from the DLL rather than explained away, and it should be root-caused before that function is trusted.
