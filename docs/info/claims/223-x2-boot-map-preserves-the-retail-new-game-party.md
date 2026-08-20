---
id: C223
kind: claim
status: holds
created: 2026-08-20
tags: boot,gameplay
depends: src/native/startup.c#x2_override_0055beb0, src/native/input_probe.c
---

## Claim

X2_BOOT_MAP preserves the retail New Game party initialization before loading the requested map

## Evidence

RE identifies BehavEd startFirstMission at FUN_004a7b10 as the owner that resets managers and assigns Magneto/Cyclops/Wolverine/Storm before menus/new_game. The repaired override calls that exact function and intercepts only its later script. Live x2ctl input measured player 0 handle 0x00000201 -> actor 0x08326010 (1 of 5 resolves); the formerly suppressed tutorial conversation 0020b entered flags 0x18 -> 0x13 (speaking and visible), not the old actorless 0x18 -> 0x10.

## What would falsify it

if a boot-map run again reaches a level with 0 of 5 hero handles resolving, or tutorial 0020b returns to flags 0x18 -> 0x10 with no selected line
