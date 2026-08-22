---
id: 101
title: RmlUi capture zeroed joystick POV to north
status: resolved
symptom: Opening the settings overlay can hold a controller d-pad north even when no control is pressed
tags: input,directinput,rmlui,joystick,pov
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The modal-input path zero-filled DIJOYSTATE2. DirectInput encodes POV north as 0 and centred as 0xffffffff; axes also need the configured range midpoint.

## Resolution

One production neutral-state writer now serves both normal joystick snapshots and modal capture: eight midpoint axes, four centred POVs and clear buttons. test_joystick_neutral poisons a full state before checking every field.
