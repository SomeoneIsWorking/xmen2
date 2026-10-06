---
id: C114
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,ark,vtable
---

## Claim

A Vulkan backend owes 209 virtual methods, not 334 and not 73. igVisualContext's vtable (libIGGfx 0x100da630) is 334 slots, of which 209 are the _purecall stub 0x100ce258 -- genuinely pure-virtual, so any concrete platform backend must supply all 209. igDxVisualContext (0x100dc438) supplies exactly those 209 and differs from the base in 291 slots. igDx8VisualContext (0x100dd0a0) overrides only 6 slots of 334, so it is a D3D version specialisation and NOT where the DirectX work lives -- replacing it would replace almost nothing. The substitution must therefore target igVisualContext's _Meta+0x3c, which currently points past igDxVisualContext straight to igDx8VisualContext. The other 125 slots are concrete platform-neutral retail engine code inherited from igVisualContext and above, so they are inherited rather than reimplemented.

## Evidence

`tools/ark_vtables.py` scans the authenticated libIGGfx image. The vtable
array ends exactly where `igDx8VertexStream`'s registered vtable begins, and
slot 333 is live through `CALL dword ptr [reg + 0x534]`. The repeated stub at
0x100ce258 jumps through IAT slot 0x100cf17c to `MSVCRT.dll!_purecall`.
Parent identification comes from slot-wise agreement across 193 vtable starts
recovered from the module's vptr stores.

## What would falsify it

Implementing the 209 and finding the engine still dispatches an unimplemented slot -- which would mean igVisualContext is not the only abstract layer in the chain, or that some caller dispatches on igDxVisualContext's interface rather than igVisualContext's. Also falsified if any of the 125 inherited slots turns out to touch the GPU.
