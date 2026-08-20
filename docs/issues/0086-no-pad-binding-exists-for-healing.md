---
id: 86
title: No pad binding exists for healing
status: resolved
symptom: the healing action has no binding at all with a pad connected -- there is no button that uses a health item
tags: pc,native,input,pad,bindings,gameplay,user-report
created: 2026-08-19
updated: 2026-08-20
---

REPORTED BY THE USER, 2026-08-19, from a real play session with a pad.

Healing (using a health item) has **no pad binding at all**. Not a wrong
button -- nothing is assigned, so the action is unreachable with a pad in hand.

## Root cause

The fixed controller preset omitted PC binding row 10, `TargetLock`. The name
is misleading in isolation: all three retained PC Defaults tables bind that
row, and the shipped PS2 potion tutorial explicitly uses `$TARGET_LOCK` as the
control that replenishes health. The Xbox controller-options package labels
Black as **Use Health Pack**; modern RB occupies Black's physical position.

An earlier investigation incorrectly treated Xbox Black and White as float
indices 8 and 9. That was not merely incomplete: `sub_00163E40` clears the
30-float array and writes only the four stick axes, while passing the digital
mask separately. C188, C189, and C192 were falsified and replaced by C225/C226.

## Resolution

`src/native/xbox_defaults.c` now maps `TargetLock` row 10 to RB DirectInput
code `0x1a`, making the preset 22 assignments. In a live initialized boot-map
run, `tools/x2ctl.py input` reported row 10 as `pad3:0x1a`; holding RB delivered
raw code `0x1a` at `+1.000` and drove player 0's corresponding physical action
slot 13 to `+1.000`. `test_xbox_defaults`, `test_player_input`, and
`test_binding_rows` pin the table and its publication path. C227 records the
evidence and the gameplay falsifier.
