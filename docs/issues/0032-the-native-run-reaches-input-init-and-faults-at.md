---
id: 32
title: The native run reaches input init and faults at NULL+0x18 because dinput8.dll cannot be loaded
status: open
symptom: *** SIGSEGV at 0x18 (not an import slot), immediately after kernel32: LoadLibraryA("C:\Windows\System32\dinput8.dll") -> NULL
tags: pc,native,input,dinput,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

`x2native --no-window --d3d8 --run` now clears module init, the CRT, engine memory, ARK, renderer init, the D3D8 device and Vulkan swapchain, the whole renderer-resource stretch (textures and their level surfaces), and reaches INPUT initialisation. There it prints

    kernel32: LoadLibraryA("C:\Windows\System32\dinput8.dll") -> NULL. That module is not one of
      the recompiled ones this host has mapped, so it genuinely cannot be loaded.

and then faults:

    *** SIGSEGV at 0x18 (not an import slot)
    [REGS] eax 00000018  ecx 00000018 ...

in `FUN_006276d0`, entered from `FUN_00551110` <- `FUN_00551ed0` <- `FUN_0061a810` <- `FUN_00554840`.

## Reading

Not a translation defect and not a bug in `LoadLibraryA`: NULL is Win32's own honest answer for a module that is not in the address space, and `0x18` is field +0x18 of a NULL pointer -- the game asks for `dinput8.dll` by absolute path, gets nothing, and does not check before using what it built from it.

This is the same shape as `d3d8.dll!Direct3DCreate8`, which the host now answers with its own `IDirect3D8` (C129): the cut is the IMPORT. There is already a partial DirectInput implementation in `src/native/dinput.c` -- the earlier run reports `DINPUT: EnumDevices(devType=2, flags=0x1) is reporting ZERO devices` from it -- so what is missing is that a dynamic `LoadLibraryA("...dinput8.dll")` + `GetProcAddress("DirectInput8Create")` does not reach it.

## Next step

Answer the dinput8 load the way the d3d8 import is answered, then let the engine's own enumeration run against the SDL3 controller backend in `src/display/`. That backend is where the project's three shipped features are meant to land, so this stop is on the critical path rather than beside it.
