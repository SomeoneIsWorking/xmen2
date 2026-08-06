---
id: C130
kind: claim
status: holds
created: 2026-08-06
tags: graphics,d3d8,vulkan
---

## Claim

The native renderer draws: a vertex buffer locked and filled through the host D3D8 COM vtables is rasterised by SDL_GPU and the pixels read back match

## Evidence

x2native --d3d8-selftest and --vk-selftest, on this machine, against a real Vulkan device. Two independent tests, both designed around their negative. (1) gpu_draw_selftest: a triangle into a 64x64 off-screen target cleared BLUE, drawn RED; centre must be red, two corners must still be blue -- so 'the triangle drew' cannot be confused with 'the clear drew', and a shader filling everything fails. (2) d3d8_draw_selftest: the same claim one layer up and through the guest's own path -- IDirect3DVertexBuffer8::Lock is dispatched through the COM vtable, the data written to the pointer it returns, Unlock dispatched, then the draw is built from a D3D8State by d3d8_drawcall.c and rasterised. It also checks the FVF decode: 0x0044 (XYZRHW|DIFFUSE) must give pos 0, colour 16, pretransformed. The shaders are GLSL compiled to SPIR-V at build time and glslc is a hard build requirement, because a binary with no shaders would link, run and draw nothing.

## What would falsify it

Both tests render one untextured triangle at a fixed size. They do NOT exercise: textures (the sampler path is bound but only ever with the 1x1 placeholder), indexed draws, mip levels, blending, or the mvp transform against a real projection. Any of those being wrong would pass here. Falsified in the direction that matters if the game reaches a draw and the frame is wrong -- at which point the failure is in the untested part, not in what these prove.
