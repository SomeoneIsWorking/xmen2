---
id: C227
kind: claim
status: holds
created: 2026-08-20
tags: input,pad,gameplay
depends: src/native/xbox_defaults.c
---

## Claim

The fixed controller preset maps PC TargetLock row 10 to RB code 0x1a, preserving the shipped console health-pack control

## Evidence

All three retained PC Defaults tables bind TargetLock; the shipped PS2 potion tutorial names TARGET_LOCK as the health-replenish control; the Xbox options screen labels Black as Use Health Pack. RB is the modern position of Xbox Black. In a live boot-map run x2ctl input reported row 10 as pad3:0x1a, 22 populated pad rows, and RB drove player 0 physical action slot 13 to +1.000. test_xbox_defaults and test_player_input pass.

## What would falsify it

a retail PC/Xbox trace proving TargetLock does not enter the health-item path, or a normal initialized gameplay run where RB reaches row 10 but cannot consume an available health item
