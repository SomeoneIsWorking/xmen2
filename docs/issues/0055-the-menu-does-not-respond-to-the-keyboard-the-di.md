---
id: 55
title: The menu does not respond to the keyboard: the DirectInput 7 device list is empty
status: open
symptom: the main menu renders and animates but no key does anything; DINPUT: EnumDevices(devType=3 KEYBOARD) is reporting ZERO devices
tags: input,dinput,menu,pc,native
created: 2026-08-11
updated: 2026-08-11
---

## What is measured, not assumed

`X2_INPUT_SCRIPT` (src/native/dinput_device.c) injects key presses into the
DirectInput 8 keyboard block at fixed times and reports each one. A run with

    X2_INPUT_SCRIPT="150+250:Space,156+250:Z,162+250:X,168+250:Tab"

produced four `DINPUT8: INJECTING ...` lines. That proves two things:

* the game POLLS the DI8 keyboard -- the injection happens inside
  `GetDeviceState`, so the line only prints because the game called it; and
* the keys are in the block when it does.

The menu did not move. Return, Space, Z, X, Tab and Down were each tried while
the menu was up (screenshots in `scratch/screenshots/`): "NEW GAME" stays
highlighted and nothing activates.

## The remaining suspect

The exe creates TWO input stacks. `DirectInputCreateEx(version=0x700)` comes
first, and its `EnumDevices` for MOUSE and for KEYBOARD both report ZERO
devices -- that is `src/native/dinput.c`, which implements the enumeration
PROTOCOL and has no device list behind it. The DI8 keyboard and mouse this
host does serve are created afterwards.

So the reading is: the menu reads the DI7 devices, finds none, and sees no
input, while the DI8 keyboard that IS wired up is polled for something else.
That is a reading, not a proof -- what would settle it is enumerating one
keyboard through `dinput.c` and seeing the menu move.

## What that needs

`m_EnumDevices` invoking the guest callback once per device with a
`DIDEVICEINSTANCEA` (580 bytes: dwSize, guidInstance, guidProduct, dwDevType,
two 260-byte name strings, guidFFDriver, wUsagePage, wUsage) in guest memory,
and `CreateDevice`/`CreateDeviceEx` answering for `GUID_SysKeyboard` and
`GUID_SysMouse`. `IDirectInputDevice7A` and `IDirectInputDevice8A` share their
vtable through `SendDeviceData`, so `src/native/dinput_device.c` should serve
both rather than being written twice.

Also unproven and cheap to rule out first: the window is HIDDEN in a headless
run and `WM_ACTIVATE` is never posted (it is `#define`d in win32_sdl.c and
never sent), so an input manager that gates on activation would also see
nothing.
