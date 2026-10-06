---
id: C112
kind: claim
status: holds
created: 2026-08-06
tags: graphics,cg,vulkan
---

## Claim

libIGGfx shades through NVIDIA Cg, not D3D8 fixed-function alone. It dynamically loads cg.dll and cgD3D8.dll (LoadLibraryA in fn 0x1002f290) and binds exactly 30 entry points -- 25 from cg.dll, 5 from cgD3D8.dll -- into a pointer table at 0x101895b8..0x1018962c. Neither DLL is statically imported by any shipped module. Failure of either LoadLibraryA, or any single GetProcAddress returning NULL, is OR-accumulated and collapsed into one status-object return (fail=[0x100cf4d0], ok=[0x100cf4d4]), so Cg is feature-detected rather than required. The bound set is dominated by REFLECTION: cgGetParameterName/Type/Resource/ResourceIndex/Direction/Variability, leaf-parameter iteration, array queries -- plus cgCreateProgram, cgGetProgramString and cgD3D8ResourceToInputRegister. Notably ABSENT are cgD3D8LoadProgram, cgD3D8BindProgram and cgD3D8SetUniform, so the engine performs its own program binding and uses Cg to learn which constant register each parameter occupies. This means a Vulkan port that implements cg.dll itself receives shaders as Cg SOURCE and never has to consume shader-model-1.x bytecode.

## Evidence

GetProcAddress pairing anchored on each CALL ESI occurring after the ESI reload at 0x1002f2d6 (32 CALL ESI total = 2 LoadLibraryA + 30 GetProcAddress): 30 calls -> 30 distinct slots, 0 unresolved, 1:1 asserted in the script rather than eyeballed. An earlier pairing that scanned FORWARD from each PUSH collided two names onto slot 0x101895b8 and was discarded as unreliable. Shader version tokens found in shipped binaries: vs_1_1 x31 + ps_1_4 x4 in libIGGfx.dll, ps_1_4 x4 in cgD3D8.dll.

## What would falsify it

A run with cg.dll present that never reaches cgCreateProgram, or discovery of a second non-Cg shading path that renders the game; also falsified if cgGetProgramString output turns out to be consumed by something other than the engine's own shader assembler.
