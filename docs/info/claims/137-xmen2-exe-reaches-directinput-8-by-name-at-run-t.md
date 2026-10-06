---
id: C137
kind: claim
status: holds
created: 2026-08-06
tags: native,input,dinput,rc-exe
---

## Claim

XMen2.exe reaches DirectInput 8 by NAME at run time, and a NULL DirectInput8Create disables the whole input subsystem rather than one device

## Evidence

Read from the image: FUN_00626bf0 builds <system32>\\dinput8.dll with GetSystemDirectoryA, LoadLibraryA's it, and GetProcAddress's "DirectInput8Create" into 0x00a6adec, storing 0 on failure (JZ at 0x00626ca7). Its ONLY user FUN_00629210 tests it at 0x00629270 and returns AL=0. Its caller FUN_0061bae0 does TEST AL,AL / JZ 0x0061bc7a at 0x0061bb32, which jumps over the ENTIRE construction of the 5x4 controller table at 0x00a68f40 (the loop at 0x0061bbf8-0x0061bc5b) plus FUN_00619bd0 and FUN_0061b030. Every later index into that table then reads NULL: FUN_0061a810 reads entry 0, ADD EAX,0x18, and FUN_006276d0 dereferences it -- the SIGSEGV at 0x18 of issue #32, verified by addr2line naming fn_XMen2_006276d0 and by ebx/edx being 0 in the fault dump. Nothing imports dinput8.dll or DirectInput8Create, so neither the IAT binder nor x86_native_thunk could ever answer; x86_native_export publishes it and 4 battery checks in case_runtime_module cover both directions, proved by mutation (dropping the export lookup fails 2 of them). With DirectInput8Create answered, the run proceeds: it enumerates GAMECTRL (zero devices) and then calls CreateDevice with GUID_SysKeyboard (0x6a15e4) and GUID_SysMouse (0x6a15f4) at 0x00628e7d / 0x00628efa.

## What would falsify it

a run where DirectInput8Create returns an object and the controller table at 0x00a68f40 is still empty -- then FUN_00629210 fails for some other reason and the dinput8 lookup was not the gate
