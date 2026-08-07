---
id: 37
title: The engine calls IDirect3DDevice8::CreateStateBlock, which the host D3D8 does not implement
status: open
symptom: x2native --d3d8 --run stops: 'the engine called IDirect3DDevice8::CreateStateBlock (slot 57, offset 0xe4), which this host D3D8 does not implement', args [0]=1 [1]=0x00a0a0a0
tags: pc,native,d3d8,graphics
created: 2026-08-07
updated: 2026-08-07
---

## Where the run gets to

Everything through issue #36: scene traversal completes and the engine reaches
state-block capture. This is not a defect -- it is a named, unimplemented D3D8
entry point, which is what the host layer is built to say.

    slot 57, offset 0xe4, arguments as pushed: [0]=0x00000001 [1]=0x00a0a0a0

`[0]=1` is `D3DSBT_ALL`; `[1]` is the out-pointer for the token.

## What is behind it

`--d3d8-permissive` walks past it (returning 0) and the run then asks for
`FindFirstFileA` -- the asset scanner, also unimplemented, also named. So there
are two ordinary work items queued here, not a fault.

## Notes for implementing it

A D3D8 state block is a recorded set of render/texture/transform state that
`Apply` replays. `src/d3d8/d3d8_state.c` already models the state the device
holds, so a block is a copy of that structure plus the four entry points
(`CreateStateBlock`, `BeginStateBlock`/`EndStateBlock`, `ApplyStateBlock`,
`CaptureStateBlock`, `DeleteStateBlock`). The token is an opaque handle, so it
can be an index into a table this layer owns -- the same shape
`src/gpu/gpu_draw.c` uses for resources, where a stale handle reports itself
rather than addressing whatever took its slot.

`D3DSBT_ALL` vs `_PIXELSTATE` vs `_VERTEXSTATE` decides WHICH state is
captured; capturing everything when the engine asked for a subset would replay
state it expected to keep, so the mask has to be honoured rather than assumed.
