---
id: C138
kind: claim
status: holds
created: 2026-08-06
tags: native,input,dinput,sdl,rc-exe
---

## Claim

The exe's keyboard and mouse come from DirectInput 8 by FIXED GUID, and their state layout is read from the game's own DIDATAFORMAT rather than assumed

## Evidence

src/native/dinput_device.c + dinput8.c m_CreateDevice; verified on the real run, which now prints 'the keyboard data format is 256 byte(s) over 256 object(s)' and 'the mouse data format is 20 byte(s) over 11 object(s)' -- read out of the DIDATAFORMAT the game passes, and matching the exe's own data at 0x6a6544 and 0x6a652c exactly. The 20/11 mouse format is DIMOUSESTATE2 (8 buttons), NOT the 16-byte DIMOUSESTATE the symbol name c_dfDIMouse suggests, so a hardcoded 16 would have written short. The GUIDs at 0x6a15e4 / 0x6a15f4 were read from the image and are GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000} and GUID_SysMouse {..2B60..}; all 16 bytes are compared because they differ only in the first dword. The vtable slot numbering is pinned by the offsets the game dispatches through (SetDataFormat 0x2c, SetCooperativeLevel 0x34, Acquire 0x1c) and CONFIRMED by the run: the sizes above could not print if slot 11 were not SetDataFormat. 13 battery checks in case_dinput drive the device through its own vtable and cover the three refusals (a state read before Acquire, an Acquire before SetDataFormat, a cbData the caller's own format did not declare); dropping two of them fails exactly two checks.

## What would falsify it

a GetDeviceState whose cbData differs from the dwDataSize the same caller declared in SetDataFormat -- then the size is not determined by the data format and reading it from there is wrong
