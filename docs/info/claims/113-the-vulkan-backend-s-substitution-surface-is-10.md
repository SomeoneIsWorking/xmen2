---
id: C113
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,ark
---

## Claim

The Vulkan backend's substitution surface is 10 abstract ARK classes in libIGGfx, and igVisualContext -> igDx8VisualContext is the top of it. libIGGfx registers 100 classes, 23 abstract, and EVERY abstract class has a _Meta+0x3c binding to a concrete one: igVisualContext and igDxVisualContext -> igDx8VisualContext; igVertexArray, igVertexArray1_1, igDxVertexArray1_1 -> igDx8VertexArray1_1; igVertexArray2, igDxVertexArray2 -> igDx8VertexArray2; igIndexArray, igDxIndexArray -> igDx8IndexArray; igVertexStream, igDxVertexStream -> igDx8VertexStream; igImage, igDxImage -> igDx8Image; plus the render-state extensions igMultiTextureExt, igPointSpriteExt, igDecalExt, igDisableExt -> their igDx8 forms. Instance sizes: igVisualContext 0x140, igDxVisualContext 0x558, igDx8VisualContext 0x558 -- the Dx8 leaf adds NO fields over its abstract parent and is purely a behaviour/vtable specialisation. The engine is already a multi-platform renderer abstraction: igCapabilityManager has igDx, igXbox, igPsx2 and igGamecube subclasses, so a Vulkan backend is the shape the design already anticipates rather than a graft.

## Evidence

tools/ark_classes.py (I033) over scratch/recomp/libIGGfx.json: 100 call sites -> 100 classes, 0 argument lists unrecovered, isAbstract vs retrieveVTablePointer==NULL disagreements 0, 29 _Meta+0x3c stores of which 23 resolve to a registered class and 6 are stores of NULL by concrete classes clearing their own slot. Substitution semantics are C008/docs/RE/ark.md, verified by decompiling igMetaObject::createInstance at libIGCore 0x10044380, which follows +0x3c in a loop.

## What would falsify it

Registering a class and repointing igVisualContext's _Meta+0x3c, then finding createInstance still returns an igDx8VisualContext or returns NULL -- which would show the binding is not the only thing selecting the implementation. Also falsified if any of these classes is instantiated through a path that does not go through igMetaObject::createInstance.
