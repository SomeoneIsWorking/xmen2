---
id: 32
title: The native run faults at NULL+0x18 in input init -- a NULL DirectInput8Create disables the whole input subsystem
status: resolved
symptom: "*** SIGSEGV at 0x18 (not an import slot), in FUN_006276d0, shortly after kernel32: LoadLibraryA(\"C:\\Windows\\System32\\dinput8.dll\") -> NULL"
tags: pc,native,input,dinput,controller,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

`x2native --no-window --d3d8 --run` now clears module init, the CRT, engine memory, ARK, renderer init, the D3D8 device and Vulkan swapchain, and the whole renderer-resource stretch (textures and their level surfaces). It reaches INPUT initialisation and faults:

    *** SIGSEGV at 0x18 (not an import slot)
    [REGS] eax 00000018  ecx 00000018  edx 00000000  ebx 00000000  esi 00000000  edi 711d8aa0

`addr2line` names the body: `fn_XMen2_006276d0`, at `MOV EAX, dword ptr [ECX]` with `ECX = 0x18`.

## The reading that was WRONG, kept because it was convincing

The line immediately above the fault is

    kernel32: LoadLibraryA("C:\Windows\System32\dinput8.dll") -> NULL

and the first version of this entry blamed it: "the game asks for dinput8.dll by absolute path, gets nothing, and does not check". **It does check.** `FUN_00626bf0` builds the path from `GetSystemDirectoryA`, and:

    00626c9a  CALL dword ptr [0x0067f044]   ; LoadLibraryA
    00626ca0  CMP EAX,EBX                   ; EBX = 0
    00626ca7  JZ  0x00626cbc                ; -> store a NULL proc pointer
    00626ca9  PUSH 0x6a5834                 ; "DirectInput8Create"
    00626caf  CALL dword ptr [0x0067f040]   ; GetProcAddress
    00626cb5  MOV [0x00a6adec],EAX

and its ONLY user tests it before calling:

    0062926a  MOV ECX,dword ptr [0x00a6adec]
    00629270  TEST ECX,ECX
    00629272  JNZ 0x00629287                ; else return AL=0

So the NULL handle is handled. Adjacency in the log is not causation, and the proximity of a loud honest message made it look like one.

## Actual cause

The caller is `FUN_0061a810`:

    0061a822  CALL dword ptr [EDX + 0x3c]   ; -> EAX, and EBX = EAX = 0
    0061a825  MOV ECX,dword ptr [0x00a6abfc]
    0061a82b  TEST ECX,ECX                  ; non-NULL, so input is "present"
    0061a83d  MOV EAX,[0x00a6ac04]          ; = 0  (edx in the dump is this, unmodified)
    0061a842  LEA EDX,[EBX + EAX*0x4]       ; = 0
    0061a845  MOV EAX,dword ptr [EDX*0x4 + 0xa68f40]   ; controller table entry 0 = NULL
    0061a84d  ADD EAX,0x18                  ; 0x18
    0061a850  PUSH EAX
    0061a851  CALL 0x006276d0               ; dereferences it

`ebx = 0`, `edx = 0` in the register dump are the caller's, because a guest-to-guest call shares the register file and `FUN_006276d0` has not written either yet. So: **entry 0 of the controller table at `0x00a68f40` is NULL.**

Which raises the next question rather than answering it: WHY is that table empty, when the code that builds it runs unconditionally as far as anyone had looked? The section below follows that branch, and it leads back to the dinput8 load after all.

## What was wrong, and the correction

Both readings above are about what the *symptom* is. The CAUSE turned out to be the dinput8 load after all, by a route neither guessed: the check exists, and its failure path **disables input wholesale**.

    00626ca7  JZ  0x00626cbc               ; LoadLibraryA failed -> [0x00a6adec] = 0
    00629270  TEST ECX,ECX / return AL=0   ; FUN_00629210 gives up
    0061bb32  TEST AL,AL / JZ 0x0061bc7a   ; FUN_0061bae0 SKIPS the whole
                                           ;   controller-table build at 0xa68f40

So the empty controller table and the missing dinput8 are the same fact, one branch apart. C137 records the chain with its falsifier.

## Fixed so far

`dinput8.dll` is now a module this host answers for, which needed two things that were each independently fatal:

* **`x86_native_export`** (`src/native/x86rt_native.c`) publishes an entry point that NOTHING statically imports. `x86_native_thunk` resolves by searching the mapped modules' import tables, which can never see a symbol the guest looks up by name at run time.
* **`module_leaf`** in `kernel32.c`: `LoadLibraryA`/`GetModuleHandleA` now compare the FILE NAME. The game passes `C:\Windows\System32\dinput8.dll`, so a comparison against a bare module name never matched.

`src/native/dinput8.c` implements `DirectInput8Create` and the `IDirectInput8` object -- QueryInterface/AddRef/Release/EnumDevices/GetDeviceStatus/RunControlPanel/Initialize. Four battery checks (`case_runtime_module`) cover the loader path in both directions, proved by mutation.

## Where the run stops NOW

    DINPUT8: DirectInput8Create(version=0x800) -> a native IDirectInput8
    DINPUT8: EnumDevices(class=4 GAMECTRL, flags=0x1) is reporting ZERO devices.
    *** DINPUT8: IDirectInput8::CreateDevice was called, and is not implemented.

Input is no longer disabled wholesale, and the game asks for devices by FIXED GUID -- no enumeration needed:

    00628e77  PUSH 0x6a15e4   ; GUID_SysKeyboard -> CreateDevice [ECX+0x0c]
    00628ea5  CALL [ECX+0x2c] ; SetDataFormat(c_dfDIKeyboard at 0x6a6544)
    00628eb8  CALL [EDX+0x34] ; SetCooperativeLevel(hwnd, 0x16)
    00628ec2  CALL [EDX+0x1c] ; Acquire
    00628ef4  PUSH 0x6a15f4   ; GUID_SysMouse, then c_dfDIMouse (0x6a652c), level 6, Acquire

## Resolved

`src/native/dinput_device.c` implements `IDirectInputDevice8` for both, backed by SDL3, and the run now completes input initialisation. C138.

* `CreateDevice` recognises the two GUIDs by all sixteen bytes -- they differ only in the first dword, so a four-byte match would make every GUID in that family look like a keyboard.
* **The state layout is not assumed.** `SetDataFormat` is handed the game's own `DIDATAFORMAT` and `dwDataSize` is read out of it, which is how the mouse turned out to be **20 bytes over 11 objects** -- `DIMOUSESTATE2`, with 8 buttons, not the 16-byte `DIMOUSESTATE` the name `c_dfDIMouse` implies. A hardcoded 16 would have written short and left four buttons as whatever was on the stack.
* The keyboard maps SDL scancodes to `DIK_*` (PS/2 set 1) through an explicit table -- the two numberings are unrelated, so a key missing from that table is a key the game can never see.
* `USER32!MapVirtualKeyA` was needed immediately after: the exe queries all 256 scancodes at init (`VSC_TO_VK` then `VK_TO_CHAR`) to build its key-name table. Implemented in `win32_sdl.c` for a **US layout**, which is a stated choice -- the game's own fixup of the result `0xb4` to an apostrophe assumes it.
* Reading a device with no SDL video subsystem up says so once and is counted, because 256 zero bytes is also what a working keyboard nobody is touching returns.

13 battery checks (`case_dinput`) drive the device through its own vtable, and the three that matter are refusals: a state read before `Acquire`, an `Acquire` before `SetDataFormat`, and a `cbData` the caller's own format did not declare. Dropping two of them fails exactly two checks.

## Where the run goes now

Past input entirely. The next historical refusal was at guest address
0x0057b02c and was not an input defect.

Joysticks are a SEPARATE path through DirectInput 7 (`src/native/dinput.c`): `igWin32ControllerManager::initializeControllers` enumerates class 4 with `createControllers` (0x100052a0), which reads `guidProduct` at `+0x14` of the `DIDEVICEINSTANCE` and then drives the same device interface. `igWin32Window::enumerateMouseAndKeyboard` (0x10005660) only sets a presence flag and returns DIENUM_STOP -- it never reads the instance.

Every unimplemented device method aborts by NAME, so the engine will keep saying which one it needs.
