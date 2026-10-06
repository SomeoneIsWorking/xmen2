---
id: C129
kind: claim
status: holds
created: 2026-08-06
tags: graphics,d3d8,architecture
---

## Claim

The renderer's cut is the Direct3DCreate8 IMPORT, not the ARK class: the engine's own igDx8 code drives a host IDirect3DDevice8 and creates a real Vulkan device with nothing substituted above it

## Evidence

Measured on the first run of src/d3d8, against the real install: 'x2native --no-window --d3d8 --run' takes the engine through its OWN unmodified igDxVisualContext::userInstantiate -- Direct3DCreate8, GetAdapterIdentifier, GetDeviceCaps -- and then through CreateDevice with the parameters the game itself computed: adapter 0, hardware vertex processing, 800x600, D3DFMT_R5G6B5, one back buffer, auto depth D3DFMT_D16, its own HWND. A real SDL_GPU/Vulkan device is created and the swapchain claimed on the guest's window. It then stops naming IDirect3DDevice8::GetBackBuffer, which is a resource this host has not written yet. NOTHING is substituted: no ARK class, no vtable replacement, no engine body re-implemented. This supersedes C128's plan of installing a device at this+0x144 from inside an ARK substitution -- the device does not have to be installed at all, because the engine installs it itself once Direct3DCreate8 answers. It also settles C113's ten-class surface at a stroke: igDx8DecalExt, igDx8VertexArray and the other eight reach the SAME device object, so none of them needs substituting. The layer is 97 device methods + 15 other interfaces, of which 29 device methods are written; every unwritten one reports its INTERFACE AND METHOD NAME rather than a slot index.

## What would falsify it

An engine method that a host device cannot honestly answer -- the clearest candidate is a LOCK on a surface whose contents the engine then reads back and depends on (GetFrontBuffer, or LockRect on a render target). That would not invalidate the cut, but it would mean that one class needs an ARK substitution alongside the device. Also falsified if the engine's Dx8 path turns out to require cg.dll, which does not load here: C112 says it shades through Cg, and initCg handling NULL has been observed but shading through it has not.
