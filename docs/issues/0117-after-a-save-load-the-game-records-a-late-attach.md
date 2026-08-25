---
id: 117
title: After a save load the game records a late-attached controller but never polls it: the deserialized input manager's poll side does not resume
status: resolved
symptom: A controller connected AFTER the game loaded a save enumerates, is recorded into the manager's table by its own callback, and its device is created and configured -- and the game reads zero buttons from it for the rest of the run; the pause menu never opens
tags: pc,native,input,pad,dinput,save,load,hotswap,user-report
created: 2026-08-25
updated: 2026-08-25
---

## User report

Controller connected after game start does nothing. Boot=Continue loads a save;
plugging a pad in afterwards leaves it dead.

## What IS proven working (2026-08-25, live)

- Hotswap admission: a pad attached mid-gameplay on a fresh boot-map run is
  admitted by the game's own enumeration and its Start opens the pause menu
  (tools/live_case.py pad-late, 8/8).
- Persisted-id adoption: a pad whose persistent id matches the stored
  controller0.id is resolved to Player 1 with NO session assignment, on a
  normal boot (pad-persisted, 7/7) -- exercised through a new announced seam,
  X2_VIRTUAL_PAD_ID, because SDL virtual pads carry no serial or path.
- The table invariant: after a save load the manager's controller table names
  no live pad; the new dinput8 check re-runs the game's enumeration, which
  records the pad and creates + configures its device (named log lines).

## What stays broken

NOTHING, measured 2026-08-25 on the current tree. This section previously
read "the game's per-frame poll loop never reads the pad"; that is no longer
what a run shows, and the paragraph is replaced rather than annotated.

## Resolution (2026-08-25)

`tools/live_case.py pad-after-load` is the case this issue asked for: boot
Continue loads the save, a synthetic pad attaches at frame 2000 -- after the
payload deserialized -- and the run is then asked what the poll side holds.
11/11, twice:

    dinput8 poll side (manager 0x7120c6d0, FUN_006285c0's own array at +0xc
    and mask at +0x129cc):
      slot 0  device 0x71801f50            attached-table yes  last frame READ
      slot 1..9  device 0x00000000  NULL -- SKIPPED by the poll loop
      1 of 10 slot(s) hold a device interface, 1 named by the attached table,
      1 read last frame (mask 0x00000001). 1 host pad(s) are connected.

and the heartbeat grew 0 -> 3810 button reads, so it is the game's own loop
and not the probe. Start at frame 2463 changed the presented frame (mean
|delta| 22.4).

The "next step" below (a write-watch on the device-pointer slots) turned out
to be unnecessary once the loop was read rather than guessed at. FUN_006285c0
walks ten slots unconditionally from manager+0xc (0x6287e6 LEA, 0x628870
CMP EAX,0xa) and skips a slot only on a NULL interface pointer (0x6287f0
TEST/JZ); there is no deserialized count and no per-slot enable, so "the game
never reads it" could only mean "that slot is NULL". It is not NULL. That
narrowing is what made a one-shot measurement decisive, and the probe it
needed is now permanent: `dinput8_controller_slots_probe`, printed at the end
of every `/input` report, every slot including the empty ones.

What CLOSED it is not established. The table-invariant check
(`dinput8_check_controller_table`, which re-runs the game's own enumeration)
is present in the run and fires, and it was present when this issue was
written too. The likeliest explanation is issue #118 -- the negative was
recorded against a `scratch/build-native` binary that predated the check --
but that is a hypothesis, not a measurement, and it is recorded as one. What
is measured is the present state and a case that will catch a regression.

## Falsifier (met)

A live run where a pad attached after a save load is polled by the game
(heartbeat counter grows by thousands) without any further host change --
observed twice, 0 -> 3810 reads. A regression would show as pad-after-load
failing, with the poll-side block naming the slot that went NULL.
