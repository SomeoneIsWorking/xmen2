---
id: C120
kind: claim
status: holds
created: 2026-08-06
tags: vulkan,graphics
---

## Claim

The game's own engine code now creates a real Vulkan device. Running --vk, the recompiled x86 engine dispatches into igVkVisualContext slot 7 (userInstantiate), which calls igVisualContext's base implementation, then SDL_CreateGPUDevice(SPIRV), which reports backend 'vulkan'. C119 applied: the host vtable is seeded from igDx8VisualContext rather than the abstract base, so 237 of 334 slots are inherited verbatim from the engine and only the 98 device-touching ones are overridden. Slot 7 also runs 11 of the engine's 13 init* helpers -- every one that touches no device -- so the render-destination pool, texture, texture-stage, lighting, material, matrix, render-list, geometry and shader tables are all initialised by the engine's own code. Still owed: initDesktopDisplayFormat, initCg, and the remaining device-touching slots, of which the engine currently asks for slot 8.

## Evidence

scratch/logs/vk12.txt against the real install: 'igVk: GPU device created -- backend "vulkan"', 'ran 11 device-free init helpers', 'setVideoMode(mode=1, flags=0x0) accepted', then the reporter naming slot 8. Battery 0 of 33 failed and ctest 5/5 in the same build; --run without --vk still reaches Direct3DCreate8, so the substitution is genuinely what changes the path.

## What would falsify it

The parameter blocks at this+0x148/0x150/0x154 are allocated at 0x200 bytes each, which is a generous BOUND and not a measured size -- if a later helper indexes past that, the size is wrong. Also falsified if a slot outside the 98 turns out to reach the device through an indirect call, which the depth-6 direct-call closure cannot see.
