---
id: C128
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,scoping,architecture
reconfirmed: 2026-08-06
---

## Claim

The renderer should be finished at the IDirect3DDevice8 boundary, not slot by slot: the 78 unguarded slots funnel into 46 device methods, one of which is over half the calls

## Evidence

Measured, not argued. The 98 device-touching slots break down 5 guarded / 78 unguarded / 15 indirect (C127). Scanning all 78 unguarded bodies for their device vtable calls gives 46 DISTINCT offsets, and the distribution is extremely skewed: offset 0xc8 (SetRenderState) accounts for 53 of the call sites, 0xfc for 14, 0xb0 for 9, and a long tail of ones and twos.

So the choice is between writing 78 slot implementations by hand, or implementing 46 COM methods behind a host IDirect3DDevice8 installed at this+0x144 and letting the engine's own 78 bodies run unmodified. The second is smaller, and it is faithful BY CONSTRUCTION -- the engine computes everything, exactly as it does on Windows -- where the first re-derives engine behaviour 78 times and each divergence shows up as subtly wrong rendering attributed to Vulkan.

The shape of the state setters confirms it. igDxVisualContext::setBlendingState is fifteen instructions: consult a global override, store the value at this+0x2e4, and call SetRenderState(0x1b /* D3DRS_ALPHABLENDENABLE */, value). There is nothing worth reimplementing in that -- only the last call needs an answer.

This supersedes the backend's original working assumption. Super-calling was adopted for the 98 and covers 5; the ARK substitution remains exactly right for getting IN and for owning construction, and it is what makes installing a host device at this+0x144 possible at all (slot 7 userInstantiate is ours). What changes is where the 78 are answered.

## What would falsify it

Finding that the 46 offsets span several DIFFERENT COM interfaces whose vtables cannot be told apart at the call site -- C108 measured 73 offsets across device, texture, surface and buffer objects, so some of these 46 may not be the device's. Each offset must be attributed to an interface before any of them is implemented; assuming they are all IDirect3DDevice8 would put methods at the wrong slots, which is the failure C108 already records for the vendored Xbox translator.

## Re-confirmed 2026-08-06

CONFIRMED BY EXPERIMENT, and more strongly than the static count suggested. A permissive staging mode was added (--vk-permissive) in which an unimplemented slot returns 0 and pops its arguments correctly -- using the per-slot RET N that tools/device_slots.py now emits -- instead of aborting, so the engine can be driven THROUGH the unwritten state calls to see whether it reaches the frame boundary.

It does not. The engine asked for slot 147, that was ignored, and the very next thing was a SIGSEGV at NULL inside Gap::Gfx::igDxVisualContext::setupTextureStages (libIGGfx 0x10044e90) -- a 130-instruction helper that is NOT one of the 334 vtable slots and is therefore INHERITED verbatim, running the engine's own code, which reaches the device itself.

That is the decisive point. Answering the renderer at the slot layer cannot be complete no matter how many of the 98 are written, because inherited engine bodies below the vtable also dereference this+0x144. The only cut that covers all of them is a real object at this+0x144. It also explains the 15 slots device_slots.py classifies as 'reaches the device only through a call': the call is to helpers exactly like this one.

So the plan is not a preference between two workable designs. Installing a host IDirect3DDevice8 is the only one of the two that can work.
