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
