---
id: 59
title: The game created the gamepad, configured it, and never read it -- Poll succeeded when it should have failed
symptom: "dinput devices: gamepad 272 byte state, NOT acquired, 0 state read(s), 6260 Poll(s), 0 Acquire(s)"
tags: pc,native,input,dinput,controller,rc-exe
status: resolved
---

## What happened

With gamepad enumeration finally implemented (`src/native/dinput_pad.c`,
`dinput8.c`), the game did everything right and then read nothing:

* `EnumDevices(class=4 GAMECTRL)` offered it one device;
* it called `CreateDevice` on the instance GUID;
* `SetDataFormat` with 272 bytes over 164 objects -- `c_dfDIJoystick2`;
* `SetCooperativeLevel(hwnd, EXCLUSIVE|FOREGROUND)`;
* `EnumObjects(DIDFT_AXIS)`, and its callback set every axis to
  `[-1000, +1000]` with `DIPROP_RANGE`.

Every step of the setup succeeded. And the pad was never acquired and never
read, for a whole run, while the keyboard and mouse were read 6,260 times.

## Cause

`IDirectInputDevice8::Poll` returned `S_OK` unconditionally.

XMen2.exe's per-frame input update is `FUN_006285c0`, and its device loop at
`0x006287f0` is:

```
006287f0  MOV EAX,[EBX]              ; the device for this slot, 10 slots
006287f2  TEST EAX,EAX / JZ          ; no device -> skip
00628808  CALL [ECX+0x64]            ; Poll
0062880b  TEST EAX,EAX
0062880d  JGE 0x00628827             ; success -> read the state
0062880f  ...
00628814  CALL [EDX+0x1c]            ; FAILURE -> Acquire, then read
...
00628832  CALL [EDX+0x24]            ; GetDeviceState(0x110 = 272 bytes)
```

**`Acquire` is only ever reached down `Poll`'s failure branch.** Real
DirectInput answers `DIERR_NOTACQUIRED` from `Poll` on an unacquired device,
and that failure is precisely how the game is told to acquire. A host that
answered `S_OK` sent it to `GetDeviceState`, which correctly refused with
`DIERR_NOTACQUIRED`, which zeroed the state -- and round again, for ever.

So the device was polled 6,260 times and the one call that would have made it
work was never made.

## Fix

`m_Poll` returns `DIERR_NOTACQUIRED` when the device is not acquired
(`src/native/dinput_device.c`). Afterwards: `gamepad 272 byte state, acquired,
6253 state read(s), 6254 Poll(s), 1 Acquire(s)` -- the same read count as the
keyboard and the mouse.

## The lesson, which is about the INSTRUMENT

The first report said `NOT acquired, 0 state read(s)` and that is all it said.
Three different causes produce exactly that line -- the game never ran its
input update, or it ran and skipped this device, or it ran and every call
failed -- and no amount of staring at it distinguishes them. Counting `Poll`
and `Acquire` separately turned one ambiguous zero into `6260 Poll(s), 0
Acquire(s)`, which names the branch. The counters are now in the report
permanently, with a note when all three are zero saying the game never touched
the device at all.

## Also fixed here: a diagnostic that buried its own run

`kernel32: ResumeThread(0x24) -- that handle names no guest thread` printed per
CALL, and the game spins on `ResumeThread` while it waits for something else:
one run wrote **5,117,138 copies** of that line and buried the reports that
would have explained it. It is now said once per handle; the total was already
in the thread report.
