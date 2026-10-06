---
id: C161
kind: claim
status: holds
created: 2026-08-12
tags: input,controller,hotswap
reconfirmed: 2026-09-22
verified_at: 2026-08-22 13:15:12
depends: src/native/dinput8_hotplug.c#dinput8_hotplug_note_game_enumeration, src/native/dinput8_hotplug.c#dinput8_hotplug_pump, src/input/controller_hotplug.c#x2_controller_hotplug_needs_admission, tests/test_controller_hotplug.c#main
---

## Claim

Controller hotswap works through the game's OWN re-enumeration routine (FUN_00628e20), not around it

## Evidence

X2_VIRTUAL_PAD=f1500 attaches a pad 1500 frames after the game enumerated; the run reports the routine found at runtime (0x00628e20 FUN_00628e20) and ends with 'gamepad acquired, 2387 state read(s), 2388 Poll(s), 1 Acquire(s)'. With f1200-2200 the state reads stop at the unplug while polls continue, so a disconnected pad reports as disconnected. The direct-callback approach was tried first and MEASURED to fail (created and configured, 0 reads) because FUN_00628b40 only stores a device whose GUID is in the table at this+0x27e8, which it writes only while this+2 is set -- and this+2 is FUN_00628e20's own argument.

## What would falsify it

A hotswap run where the report shows the pad acquired but with a state-read count far below the frames since it arrived, which would mean the game admitted it and is not polling it. Or a GAMECTRL EnumDevices that returns anywhere but 0x00628e57 on the verified image -- the host refuses and prints that address, because acting on a wrong entry would call an arbitrary routine with a fabricated argument.

## Re-confirmed 2026-08-22

The original late-attach/detach live run remains the production observation. The changed admission policy still calls the same runtime-discovered FUN_00628e20(TRUE); test_controller_hotplug now proves one admission per inventory generation through twelve attach/detach cycles, while unchanged polls admit zero.

## Restored 2026-09-22

The routine is no longer discovered through a symbol table. `dinput8_hotplug.c`
declares FUN_00628e20 and its EnumDevices(GAMECTRL) call site 0x00628e57 as
recovered metadata and admits the entry only when the game's own call is seen
returning to that site; any other caller is refused by name. `tools/live_case.py
pad-late` passes 8/8 (HOTSWAP calls 0x00628e20, Start moves the frame by 10.6)
and `pad-persisted` 9/9, where the late pad's guest slot goes from -1 to 0 at
the admission.
