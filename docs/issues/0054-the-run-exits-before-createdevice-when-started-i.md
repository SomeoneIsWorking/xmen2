---
id: 54
title: The run exits before CreateDevice when started in the background without X2_SHOT
status: open
symptom: x2native --run --d3d8 ends in about a second with 'no device was ever created -- the engine did not get as far as CreateDevice'; the same command in the foreground runs to the menu
tags: native,startup,intermittent,timing
created: 2026-08-11
updated: 2026-08-11
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

## What is known

The guest returns from its main and the shutdown report prints in full -- this
is the game DECIDING to quit, not a crash. The last thing before it is the
X2_UNPACED message (the frame-cap patch firing on the first clock read) and
279 registry reads, 247 of which found nothing. `GetDeviceCaps` is never
reached; in a working run it is the next thing after the DirectX check.

## What is NOT the cause

* A leftover x2native holding a single-instance mutex: `ps` shows none, and no
  CreateMutex call appears in either log.
* The stored registry (`scratch/saves/registry.txt`): unchanged since Aug 7 and
  identical for both.

## Where to look next

The exe reads settings and quits before touching the display. The boundary ring
(`X2_NATIVE_TRACE=ON`, already on in scratch/build-native) with a SIGTERM dump
of the failing run against a working one should name the branch. Suspect
anything early that reads the environment or a handle whose value differs in a
background process group.

Not blocking: the foreground run, `./run.sh`, and every screenshot path work.
