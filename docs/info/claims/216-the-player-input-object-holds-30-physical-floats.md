---
id: C216
kind: claim
status: holds
created: 2026-08-18
tags: input,alchemy
depends: src/native/input_probe.c
---

## Claim

The player input object holds 30 physical floats at +0x2fc, the same layout the Xbox build uses (C192), reachable on PC as manager 0x0079ebc0 + 0x3c + player*0x390. Physical source 4 is the accept/LowAttack input and source 0 the movement axis. The logical action mask that FUN_005d4970 tests with '1 << action' is a separate 32-bit value from the same object's vtable +0x18, and it is cleared before the next frame's input poll, so it reads 0 at the DirectInput pump point even while an input is held.

## Evidence

Live: with Return held, player 0's +0x2fc array showed [4]=1.000 and 0 of 30 otherwise when idle; with the pad preset fixed, pad A gave the same [4]=1.000 and leftx=-1 gave [0]=-1.000. The mask read 0x00000000 in every sample including ones where the conversation demonstrably advanced that frame.

## What would falsify it

if a sample point after the game's input update ever shows the mask non-zero, the 'cleared before the poll' half is wrong and the sample point, not the mask, was the problem
