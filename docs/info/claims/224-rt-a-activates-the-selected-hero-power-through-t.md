---
id: C224
kind: claim
status: holds
created: 2026-08-20
tags: input,pad,gameplay
depends: src/native/dinput_pad.c#dinput_pad_axis, src/native/xbox_defaults.c
---

## Claim

RT+A activates the selected hero power through the Xbox-style pad preset

## Evidence

In a boot-map tutorial1 gameplay run, tools/x2ctl.py input measured right trigger code 0x06 and A code 0x15 at +1.000 simultaneously; synchronized screenshots at 0.35s and 1.85s into the hold show the selected hero progress through the power-cast pose and scene effect. Issue #85 is resolved.

## What would falsify it

a normal gameplay run with an initialized hero where the same full-scale RT+A inputs produce no cast and a keyboard power control proves the hero can use that power
