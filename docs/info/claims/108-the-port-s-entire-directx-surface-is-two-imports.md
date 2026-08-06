---
id: C108
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,graphics,scoping
reconfirmed: 2026-08-06
---

## Claim

The port's entire DirectX surface is two imports, and the vendored D3D8 translator is Xbox-shaped rather than PC-shaped

## Evidence

Measured across every shipped module: the ONLY DirectX imports in the whole game are libIGGfx.dll -> d3d8.dll!Direct3DCreate8 and libIGDisplay.dll -> DINPUT.dll!DirectInputCreateEx. Everything else reaches DirectX through COM vtables on the objects those two calls return, so the host boundary is two entry points rather than a wide API. That makes the D3D8-level boundary far narrower than 'implement D3D8' suggests. SECOND finding, and it cuts the other way: vendor/xboxrecomp/src/d3d is a 6741-line D3D8 implementation with a POSIX/OpenGL backend (d3d8_gl.c) and real COM objects (IDirect3DDevice8Vtbl in d3d8_xbox.h), which looks reusable -- but its vtable is the XBOX D3D8 layout, not the PC one. PC's IDirect3DDevice8 runs Release, TestCooperativeLevel, GetAvailableTextureMem, ResourceManagerDiscardBytes, GetDirect3D...; the vendored one runs Release, GetDirect3D directly and omits the cursor and additional-swap-chain methods entirely. Slot N therefore means different methods in the two, so libIGGfx calling through a PC vtable would land on the wrong Xbox function. The BODIES (state translation, combiners, shaders, the GL backend) are reusable; the interface layer is not.

## What would falsify it

The vtable comparison is against my knowledge of PC D3D8's method order, not against a header or a diff with the shipped d3d8.dll's interface. Before any work is planned on it, that order should be checked against a real d3d8.h or against what libIGGfx actually calls -- the offsets it uses on the device object are measurable from the recompiled code and would settle it from the game's side rather than from memory.

## Re-confirmed 2026-08-06

Falsifier RESOLVED, and from both sides rather than from memory. FROM THE GAME: Gap::Gfx::igDxVisualContext::userInstantiate at 0x1002c210 calls Direct3DCreate8, stores the result, then makes its first vtable call as  with three arguments pushed plus . PC D3D8's IDirect3D8 slot 0x34 is GetDeviceCaps(Adapter, DeviceType, pCaps) -- three arguments. The layout is confirmed from the caller, not recalled. FROM THE VENDORED SIDE: its IDirect3D8Vtbl has FOUR entries -- QueryInterface, AddRef, Release, CreateDevice -- so it is 16 bytes long and offset 0x34 is off the end of it entirely. The interface is therefore not merely reordered relative to PC D3D8, it is a minimal subset covering what the Xbox recomp happened to need. SIZE, measured: the igDx* wrapper classes make 648 indirect calls at 73 distinct vtable offsets <= 0x18c, across 219 functions. Those offsets span the whole COM family (device, texture, surface, vertex and index buffers), not one interface, so 73 is the total method surface to implement rather than 73 device methods.
