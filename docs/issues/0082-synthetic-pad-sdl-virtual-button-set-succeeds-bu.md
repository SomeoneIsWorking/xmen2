---
id: 82
title: Synthetic pad: SDL virtual button set succeeds but the joystick reads UP inside the game
status: investigating
symptom: tools/x2ctl.py pad a reports 'joystick button 0 set (joystick itself reads UP)'. The game polls hard -- 71,700 button reads and 43,020 axis reads in one run -- and every read comes back released, so a gamepad press never reaches gameplay, while key Return advances the same conversation in the same run.
tags: input,pad,sdl,dinput,native
created: 2026-08-18
updated: 2026-08-18
---

## What is measured

`SDL_SetJoystickVirtualButton(g_virt_js, 0, true)` returns **true**, then
`SDL_UpdateJoysticks()` + `SDL_UpdateGamepads()`, then
`SDL_GetJoystickButton(g_virt_js, 0)` returns **0** -- one layer BELOW the
gamepad mapping, so this is not a mapping problem.

Repro, no hardware needed:

```sh
X2_BOOT_MAP=act0/tutorial/tutorial1 X2_VIRTUAL_PAD=1 X2_UNPACED=1 \
    scratch/build-native/x2native --no-window --control &
tools/x2ctl.py pad a        # reports the read-back
tools/x2ctl.py key Return   # control: this DOES advance the conversation
```

## Already ruled out -- do not re-derive

- **SDL itself and the mapping.** `tests/test_virtual_pad.c` performs the exact
  same attach/map/open/set/update/read sequence standalone and all ten buttons
  round-trip through BOTH layers, including the `start:b5` that SDL enumerates
  as `START=6`. Passes with `SDL_INIT_GAMEPAD` alone and with
  `SDL_INIT_GAMEPAD|SDL_INIT_VIDEO|SDL_INIT_EVENTS`.
- **SDL version.** Game and test both link /lib64/libSDL3.so.0 (3.4.14).
- **Thread.** Attach and press are on the same host thread (logged, identical).
- **Handle validity.** At press time: id 1 (attached as 1), connected=1,
  virtual=1, 11 buttons, 6 axes.
- **Device identity.** `pad_open` calls `SDL_OpenGamepad(id)` with the same
  `SDL_JoystickID` the virtual joystick was attached as.
- **SDL hints / event toggles.** The tree sets none
  (`SDL_SetJoystickEventsEnabled`, `SDL_SetHint`: no call sites).
- **A missing pump.** That WAS a real defect and is fixed -- the gamepad path
  never called `SDL_UpdateGamepads` where the keyboard and mouse paths always
  pumped. SDL is now refreshed 7,170 times per run and buttons still read up,
  so the pump was necessary and not sufficient.
- **Expiry racing the press.** The read-back is synchronous, in the same
  function, before any frame tick can run `virtual_expire`.

## Where to look next

What differs between the passing standalone test and the failing game is no
longer any of the above, so the remaining candidates are about the state of
SDL's joystick subsystem in a long-running process: whether some other open of
the same device (the gamepad opened by `pad_open`) leaves the virtual driver's
pending state being overwritten on update, and whether `SDL_UpdateJoysticks`
inside a process that also pumps SDL constantly from the keyboard path behaves
differently from one that does not.

Bisect by making `tests/test_virtual_pad.c` progressively more like the running
game -- that is the cheap direction, because the test is instant and the game
costs ~35 s per attempt.

## Note

A REAL controller was never tested against the pump fix: this machine has no
gamepad attached (`/sys/class/input/*/name` lists none). The pump fix is
correct on its own terms -- the keyboard and mouse paths have always pumped --
but its effect on real hardware is unverified.

### Note (2026-08-18)
2026-08-18: AXES WORK, BUTTONS DO NOT -- the clearest split yet.

In-game, `tools/x2ctl.py pad leftx=-1` reports: axis 0 set to -32767, joystick reads -32767, gamepad "leftx" reads -32767. The identical sequence for a button reports the joystick still UP. Same handle, same thread, same update calls, one attached device. So the virtual device IS live and being updated in the running game; the failure is specific to SDL's virtual BUTTON path there.

Further ruled out since: a flooded SDL event queue (65,533 queued events, both axis and button still work standalone), 0/1/1000 pumps with and without draining events, and a conflicting `true`/`bool` macro (none in the tree; dinput_pad.c includes only dinput_pad.h, guest_clock.h, stdio/stdlib/string and SDL.h).

Expiry is NOT the cause: the release tick fires 0.001s past its 0.3s deadline, i.e. correct behaviour, long after the read-back already reported UP. The button is never down at any point -- 71,700 polls saw 0 down.

ALSO FIXED here: this hunt was slowed by the reason buffer being reused, so an axis request answered with the previous BUTTON call's text ("joystick button 0 set ... gamepad a") and I read it as an axis result. Every exit now stamps the buffer up front. That is the second time an instrument in this area invented an observation it never made; the first was an axis counter declared and never incremented.
