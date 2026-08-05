---
id: C053
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/main.c, xbox/CMakeLists.txt
---

## Claim

Rendering needs the NV2A hook, not D3D API overrides. X-Men Legends II's statically-linked Xbox D3D8 library drives the GPU push buffer and registers directly, and the toolkit ships xemu's NV2A emulation for exactly that -- but xbox/src/main.c passed every 0xFD000000 fault straight through, so no draw command could reach a GPU.

## Evidence

The hottest D3D entry point called from game code (0x003F06C0, 33 call sites) is a push-buffer emitter: it loads a pointer from 0x3FF940, compares against a limit at 0x3FF944, writes two dwords and advances by 8. vendor/xboxrecomp/src/nv2a provides nv2a_hook_init + nv2a_hook_handle_mmio (a VEH x86-64 instruction decoder) and nv2a_pgraph_d3d11.c, which despite its name emits calls on the toolkit's IDirect3DDevice8 -- OpenGL-backed on Linux via src/d3d/d3d8_gl.c. main.c's VEH now routes GPU register and VRAM faults there and REPORTS any instruction it cannot decode instead of resuming past a lost write. The run reaches 469 kernel calls and 4067 indirect calls with the hook installed.

## What would falsify it

no GPU register fault has been decoded yet -- the title stops on an unresolved indirect call before it draws. Until a '[NV2A]' line or a rendered frame appears, this is a wiring claim, not a rendering one.
