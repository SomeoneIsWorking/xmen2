---
id: C119
kind: claim
status: holds
created: 2026-08-06
tags: vulkan,graphics
---

## Claim

The Vulkan backend's real obligation is 98 methods, not 209, if it inherits igDx8VisualContext rather than igVisualContext. C114 counted igVisualContext's 209 pure-virtual slots, which is what a backend owes when it starts from the ABSTRACT class. But most of what igDxVisualContext implements is platform-neutral bookkeeping over its own fields (this+0x170..0x17c: native window, render-destination list, current flags), not DirectX. Following direct calls to depth 6 from each of igDx8VisualContext's 334 slots and testing for a reference to the device fields this+0x140/0x144/0x148/0x14c: 98 slots reach them, 236 do not, and 72 of the 98 are among C114's pure set. So seeding a host vtable from igDx8VisualContext and overriding only the device-touching slots is 98 methods. Supporting shape: of the 209 pure slots, 70 have implementations of 8 instructions or fewer and 55 have 3 or fewer -- they are accessors like getNativeWindow (MOV EAX,[ECX+0x170]; RET), not renderer logic.

## Evidence

A transitive scan over the authenticated libIGGfx instruction stream used the
vtable recovered by `tools/ark_vtables.py`; the instruction-count distribution
came from the same retail bodies. A running-engine check with a vtable seeded
from `igVisualContext` inherited 125 slots and owed 209 as C114 predicts. Its
first two demands were slot 254 (`igDxVisualContext::setVideoMode`, `RET 0x8`,
returning the OK status singleton) and slot 44 (`createRenderDestination`, 109
instructions).

## What would falsify it

The scan follows DIRECT calls only and stops at depth 6, so a slot reaching the device through an indirect call or a deeper chain is counted as clean -- 98 is a LOWER bound. Falsified if inheriting igDx8VisualContext and overriding those 98 still reaches a D3D call, which would mean the closure missed a path.
