---
id: C127
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,scoping
---

## Claim

Super-calling covers only 5 of the renderer's 98 device slots: the other 78 dereference the device unguarded and must be implemented, not inherited

## Evidence

Measured by tools/device_slots.py, which now reports per slot whether every read of a device field (this+0x140/0x144/0x148/0x14c) is NULL-checked within three instructions by a TEST of the same register followed by a conditional jump -- the exact pattern the engine uses. Of the 98 device-touching slots of igDx8VisualContext: 5 GUARDED (30 open, 34 getLastError, 36 setNativeWindowHandle, 50 setRenderDestinationSize, 331 setVertexBlendingShaderManager_Dx), 78 UNGUARDED, and 15 with no direct device read at all (they reach it through a call, so the question does not apply at this level).

This corrects the working assumption of the backend's design. 'Super-call the engine's own body and program the GPU from the state it computed' was adopted because igDxVisualContext::setViewport is 395 instructions of clamping and one device call -- true, and it is one of the 15 -- but it generalises to far fewer slots than that example suggested. The 78 have to be implemented directly, which for most of them means reading the state they were about to hand to SetRenderState and recording it in a host-side mirror the draw path consumes.

The detector is deliberately conservative in the safe direction: a slot reported UNGUARDED may still be safe, but one reported guarded has the check in the instruction stream. Being wrong the safe way costs a transcription; the other way costs a SIGSEGV, which is how checkAndCreateSurfaces was found.

## What would falsify it

A slot in the guarded five faulting on a NULL device at runtime, which would mean the three-instruction window or the same-register requirement is missing a form of the check. Four of the five (30, 34, 36, 50) are implemented and exercised on every run; 331 is not yet reached.
