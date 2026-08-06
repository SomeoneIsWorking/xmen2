---
id: 18
title: The game's DirectX 9.0c presence check fails, as it truthfully should
status: open
symptom: MessageBox 'DirectX not found -- DirectX 9.0c or higher is not installed on this computer' from XMen2.exe FUN_00617480, after the native run clears the CRT, registry and COM surfaces
tags: pc,recomp,native,graphics,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## What this is

Not a defect. The run now gets far enough to reach the game's own environment
validation, and that validation is CORRECT: there is no DirectX on this host,
and the port has not yet provided the graphics layer that would make the answer
different.

## The mechanism, read out (2026-08-06)

`FUN_00617480` is the check, and it works like this:

    read  HKCU\...\X-Men Legends 2\Settings\DXChecked
    if it is 1                      -> skip the probe entirely
    push 9, 0, 0x63                 -> "9.0c"
    call FUN_00616f50               -> the COM query; CoCreateInstance of
                                       CLSID {A65B8071-3BFE-4213-9A5B-491DA4461CA7}
    test al,al
      non-zero  -> write DXChecked = 1, continue
      zero      -> ShowCursor, MessageBox "DirectX not found"

So the COM object IS the version reporter, and `REGDB_E_CLASSNOTREG` is the
direct cause of the message. (An earlier note here said the message box came
from a separate check; it does not.)

**`Settings\DXChecked` is a lever, and it must not be pulled yet.** Setting it
to 1 makes the dialog disappear and the game proceed -- straight into the first
real D3D call, with the clean diagnosis thrown away. It is the game's own cache
of a check that PASSED, and here the check has not passed and should not.

The same goes for returning a fabricated success from `CoCreateInstance`: it
would satisfy a version query about a DirectX that is not there.

## What would make the answer change

Only a real graphics layer. The ordering in the section above is unchanged, and
this narrows step 1: the probe is a COM query for a DirectX version, so the
decision is not "how do we answer this call" but "where does the port's D3D
boundary sit" -- and once that exists, either the query can be answered
truthfully or the whole check is bypassed at the engine level via the ARK
substitution point, which is a design decision rather than a lie.

## The decision it forces

This cannot be answered by making the check pass. What the check is really
asking is "is there a D3D9 I can use", and the port's answer has to be a real
graphics layer, not a yes.

The project's direction (docs/strategy.md) is SDL3 standing in for D3D8, reached
through native overrides at the engine's own abstraction rather than by faking
the Win32/COM layer beneath it. So the honest ordering is:

1. Find what the check actually probes (a d3d9.dll load? a registry key? the
   CoCreateInstance that already fails?) -- FUN_00617480 is the function.
2. Decide where the port's graphics boundary sits: at D3D8 calls, or higher, at
   igDisplay/igGfx via the ARK `_Meta+0x3c` substitution point that
   docs/RE/ark.md documents for exactly this.
3. Only then does the check have a truthful answer.

Making MessageBoxA's caller skip the dialog, or returning a fabricated success
from whatever it probes, would move the failure to the first real D3D call and
lose the clear diagnosis.

## Scoping the boundary, measured (C108)

**The whole DirectX surface is two imports.** Across every shipped module:

    libIGGfx.dll    -> d3d8.dll!Direct3DCreate8
    libIGDisplay.dll -> DINPUT.dll!DirectInputCreateEx

Everything else goes through COM vtables on the objects those return. So the
D3D8-level boundary is two entry points plus the methods the game actually
calls -- much narrower than "implement D3D8" sounds.

**The vendored translator is a real asset, but not a drop-in.**
`vendor/xboxrecomp/src/d3d` is 6741 lines with a POSIX/OpenGL backend
(`d3d8_gl.c`) and genuine COM objects. Its vtable, however, is the **Xbox** D3D8
layout:

    vendored (Xbox):  ... Release, GetDirect3D, GetDeviceCaps, ...
    PC D3D8:          ... Release, TestCooperativeLevel, GetAvailableTextureMem,
                          ResourceManagerDiscardBytes, GetDirect3D, ...

and it omits the cursor and additional-swap-chain methods. Slot N is a different
method in each, so libIGGfx calling through a PC vtable would land on the wrong
function. The bodies -- state translation, combiners, shaders, the GL backend --
are reusable; the interface layer has to be rebuilt to the PC layout.

That is the trade to weigh, and it is now measured rather than guessed.
