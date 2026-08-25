---
id: 117
title: After a save load the game records a late-attached controller but never polls it: the deserialized input manager's poll side does not resume
status: investigating
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

Even with the table repaired and the device created, the game's per-frame poll
loop never reads the pad: 'the game read a button N time(s)' stays at the
probe-only baseline for the rest of the run, and Start does nothing. The same
save loaded through the MANUAL menu with the pad attached BEFORE the load polls
~90,000 reads -- so the load itself is not the breaker; the state is 'pad first
seen by the manager AFTER the payload deserialized'.

## Measured shape of the manager (from the emitted bodies)

The per-frame update FUN_006285c0 walks twelve device slots (keyboard +4,
mouse +8, then ten controller pointers, state buffers stepping 0x110 =
sizeof DIJOYSTATE2), each guarded by a pointer compare before the
GetDeviceState vtable call. Hypothesis: the save payload deserializes those
per-slot device pointers / per-slot state, and the enumeration callback's
record path does not restore the poll-side slot the deserialization owns.
Retail does not need this because Windows DirectInput instance GUIDs are
persistent, so a restored identity matches the replugged device.

## Next step (oracle, not more reading)

X2_WRITE_WATCH on the manager's device-pointer slots (manager+0xc .. +0x30)
across a post-load admission vs a pre-load admission; the writer that refuses
to store the new interface names the gate. Then check the stock control: does
the retail game under Wine poll a pad attached after a save load? If it does,
capture what its manager state has that ours lacks.

## Falsifier

A live run where a pad attached after a save load is polled by the game
(heartbeat counter grows by thousands) without any further host change.
