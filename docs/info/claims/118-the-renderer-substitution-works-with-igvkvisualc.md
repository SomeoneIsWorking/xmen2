---
id: C118
kind: claim
status: holds
created: 2026-08-06
tags: vulkan,graphics,ark
---

## Claim

The renderer substitution WORKS: with igVkVisualContext bound in, the engine never calls Direct3DCreate8 and dispatches into host code instead. Measured: --vk log contains 0 occurrences of Direct3DCreate8 where --run contains 1, and the engine reaches igVkVisualContext vtable slot 1. THREE things had to be right and two were wrong first. (1) The whole chain must be rebound, not just the abstract root: createInstance follows _Meta+0x3c from whichever meta it was handed, so binding igVisualContext alone leaves igDxVisualContext::_instantiateFromPool using its own meta, which still resolved to igDx8VisualContext -- the binding installed and the game built a DirectX context anyway. (2) Readiness is the CONCRETE class registering, not the abstract one: igVisualContext registers well before its DirectX subclasses, so binding when it appears finds the other two unregistered. (3) igObject's 21-slot do-nothing map (C116) does NOT apply wholesale to igVisualContext: slots 0,2,3,4,5,6,9..19 are import thunks into libIGCore and are genuinely igObject's behaviour, but slots 1 (userAllocate), 7 (userInstantiate), 8 (userRelease) and 20 (getClassMeta) are overridden by igVisualContext. Slot 7 is the one igDxVisualContext overrides to call Direct3DCreate8, so stubbing it as an igObject no-op would silently skip renderer creation rather than implement it.

## Evidence

scratch/logs/vk4.txt against the real install: all three metas rebound (igVisualContext 0x00a9df10, igDxVisualContext 0x00aaa118, igDx8VisualContext 0x00a81b50), 18 of 334 slots implemented, and the run stops at 'the engine dispatched vtable SLOT 1'. grep -c Direct3DCreate8 is 0 for --vk and 1 for --run in the same build. Slot classification read from igVisualContext's own vtable at libIGGfx 0x100da630, printing each slot's target body rather than inferring from names.

## What would falsify it

A path that instantiates a visual context without going through igMetaObject::createInstance would bypass all three bindings; also falsified if any of the 316 still-unimplemented slots turns out to be reached before the renderer can meaningfully answer it.
