---
id: C160
kind: claim
status: holds
created: 2026-08-12
tags: input,controller
---

## Claim

The game's gamepad path runs end to end: enumerate, create, c_dfDIJoystick2, per-axis range, acquire and read every frame

## Evidence

A run with X2_VIRTUAL_PAD=1 reports 'gamepad 272 byte state, acquired, 6253 state read(s), 6254 Poll(s), 1 Acquire(s)' -- the same read count as the keyboard and mouse. The exe's own sequence was read out of the binary first (FUN_00628b40 enumerate/create/SetDataFormat(0x006a6514)/SetCooperativeLevel/EnumObjects, FUN_00628510 DIPROP_RANGE [-1000,1000], FUN_006285c0 the per-frame Poll-then-GetDeviceState(0x110) loop), so what is served is what the game asks for rather than what seemed reasonable. test_dinput_pad passes 141 checks including a no-pad negative control.

## What would falsify it

A run whose report shows the gamepad acquired but with a state-read count far below the keyboard's -- that would mean the per-frame loop is skipping it on some frames. Or an in-game action that does not respond to the pad, which would mean the DirectInput layout served (Xbox 360: sticks on X/Y and Rx/Ry, triggers combined on Z, d-pad as POV 0, 10 buttons) is not the one this game's mapping was written for.
