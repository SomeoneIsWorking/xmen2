---
id: C108
kind: claim
status: holds
created: 2026-08-06
tags: pc,pe,graphics,scoping
reconfirmed: 2026-08-06
---

## Claim

The retail PC modules import only two DirectX entry points; the remaining
surface is reached through PC COM vtables.

## Evidence

Across every shipped PC module, the only DirectX imports are
`libIGGfx.dll -> d3d8.dll!Direct3DCreate8` and
`libIGDisplay.dll -> DINPUT.dll!DirectInputCreateEx`. Everything else reaches
DirectX through COM vtables on the returned objects. The `igDx*` wrapper
classes make 648 indirect calls at 73 distinct vtable offsets up to 0x18c,
across 219 functions and the device, texture, surface, vertex-buffer, and
index-buffer interfaces.

## What would falsify it

A shipped module importing another DirectX symbol, or a direct call that does
not pass through either imported factory or a resulting COM object, would
invalidate the boundary inventory.

## Re-confirmed 2026-08-06

`Gap::Gfx::igDxVisualContext::userInstantiate` at libIGGfx 0x1002c210 calls
`Direct3DCreate8`, stores the result, then calls vtable offset 0x34 with three
arguments plus `this`. PC D3D8 defines that slot as
`GetDeviceCaps(Adapter, DeviceType, pCaps)`, confirming the PC ABI directly
from the retail caller.
