---
id: C215
kind: claim
status: holds
created: 2026-08-18
tags: input,binding
depends: src/native/input_bindings.c, src/native/xbox_defaults.c
---

## Claim

XMen2.exe keeps sixteen controller binding sets in four banks: masters 0..3 (options UI + registry), working copies 4..7 (the ONLY ones FUN_006285c0 evaluates), 8..11, and menu copies 12..15. FUN_0061b030 copies each master into 4..7 and 12..15 once, then overwrites slots 2 and 3 of those copies with its own hardcoded keys -- row 4 slot 2 is DIK Return. Slot 1 defaults to 0 on every row and is the free alternate binding, persisted as Controls\Player%d\<row>2.

## Evidence

Read out of FUN_0061b030 (copy loop at 0x0061b6a8 via FUN_00629490; menu-key writes at 0x0061b83f onward) and confirmed live: the DirectInput wrapper's evaluation list at [wrapper+0x129d0] held sets 4..7 while the populated table was set 0's; controller 4 row 4 showed slot0 kb 0x4b, slot2 kb 0x1c, slot3 kb 0x9c and slot1 empty. Installing the Xbox preset into slot 1 of sets 0/4/12 made pad A set player 0 physical[4]=1.000 and advance the tutorial conversation; leftx=-1 set physical[0]=-1.000.

## What would falsify it

if a build is found where FUN_006285c0's tail walks a list that includes a master set, or where slot 1 carries a non-zero default in FUN_0061b030
