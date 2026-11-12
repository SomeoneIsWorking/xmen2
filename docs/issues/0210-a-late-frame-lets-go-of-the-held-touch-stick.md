---
id: 210
title: a late frame lets go of the held touch stick
status: resolved
symptom: "After the host presses Continue through the tutorial's opening conversation, the client pushes its stick in free play and no hero moves on either screen."
state_items: S023
tags: lan,input,touch
created: 2026-11-12
updated: 2026-11-12
---

# 0210 — a late frame lets go of the held touch stick

## Observed

Two players by friend code on one host (`scratch/lan-pub/measure.py`, run 39
on 2026-11-12). The host pressed A through the opening conversation; both
reached free play with the touch stick drawn. 3 s later B held its stick left
for 1.5 s and no hero moved on either side's `/party`. B's touch summary
published 6 axis changes and let the held stick go once "with the overlay
hidden"; the game read the axis off centre once.

Later pairs (runs 40 to 42) sampled both machines every 0.1 s on one wall
clock. B moved Cyclops with touch and with a pad on every push that had room,
and both screens agreed to within a frame. Pushes that moved nothing ran into
a wall or came after retail's own "use C to regroup" tutorial popup.

## Cause

`src/input/gameplay_control.cpp:x2_gameplay_control_state` called the HUD
gone 0.25 s of guest time after its last draw, whatever the frame count. B
presented frames of 229, 182 and 294 ms right after the push, so the touch
runtime read the overlay as hidden mid-frame and cancelled the stick. Any
host hitch does this in single player too.

## Fix

The gate is keyed on presented frames (`gpu_frames_presented`): it says yes
while the HUD drew in the frame in progress or the one before, and no after a
whole frame without it.

Proven 2026-11-12 by `tools/live_case.py stick-hitch`. It holds the stick
down, then stops the game process three times for 0.4 s each. Before the fix
the stick read (0, 0) afterwards and the hero stood still. After it the stick
read (0, 1) and Magneto walked 136 units.
