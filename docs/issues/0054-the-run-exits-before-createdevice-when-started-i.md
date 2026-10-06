---
id: 54
title: The run exits before CreateDevice when started in the background without X2_SHOT
status: resolved
symptom: x2native --run --d3d8 ends in about a second with 'no device was ever created -- the engine did not get as far as CreateDevice'; the same command in the foreground runs to the menu
tags: native,startup,intermittent,timing
created: 2026-08-11
updated: 2026-08-14
---

## Repro, and it is deterministic per shape

    # WORKS -- reaches the menu, 600+ draws
    X2_UNPACED=1 X2_HEARTBEAT=60 timeout 40 x2native --no-window --d3d8 --run > log 2>&1

    # FAILS -- exits in ~1s, "no device was ever created"
    X2_UNPACED=1 X2_HEARTBEAT=60 x2native --no-window --d3d8 --run > log 2>&1 &

Three of each, 2026-08-11: 3/3 foreground reached CreateDevice, 3/3
backgrounded did not. Backgrounded runs that ALSO set `X2_SHOT` reached the
menu and rendered (that is how every screenshot in scratch/screenshots was
taken), so the trigger is not backgrounding alone.

## Root cause

The native override of the DirectX 9.0c presence check left EAX untouched,
although WinMain does `CALL 0x00617480; TEST AL,AL`. The original check's true
path ends with `MOV AL,1`. A leftover zero low byte therefore made WinMain set
the graphics-failure flags, skip its entire main loop, and return 0 without the
display-failure dialog. Backgrounding and scheduler settings only perturbed
the leftover register value; they were never the cause.

## Resolution

The override writes AL=1 exactly as the original true path does. With
`X2_QUANTUM=0`, the configuration that failed 18 of 19 runs before the fix,
CreateDevice was reached in 4 of 4 runs afterward. Subsequent full loops also
reach CreateDevice and close the route. This established the wider rule now
enforced by the override tests: reproduce a guest function's return value as
well as its stack effect, using the call site rather than a guessed signature.
