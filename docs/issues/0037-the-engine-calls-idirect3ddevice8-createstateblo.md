---
id: 37
title: The engine calls IDirect3DDevice8::CreateStateBlock, which the host D3D8 does not implement
status: resolved
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


## Resolved

`src/d3d8/d3d8_stateblock.c`. A block is a whole copy of `D3D8State` plus a
generation-tagged token, so a stale token reports itself rather than addressing
whatever took its slot -- the same shape `src/gpu/gpu_draw.c` uses for
resources.

Deliberately NOT done, each refusing by name rather than approximating:

* **`D3DSBT_PIXELSTATE` / `D3DSBT_VERTEXSTATE`** capture documented SUBSETS.
  Capturing everything instead would replay state the engine deliberately kept
  out of the block, and the wrong picture would appear at some later draw with
  nothing linking it back. The game asks for `D3DSBT_ALL` (`[0]=1`).
* **`BeginStateBlock`/`EndStateBlock`** (the recording form) need the device's
  setters to write into a block instead of the device -- a different mechanism,
  not a bigger version of this one. Their vtable slots stay NULL, so reaching
  them is reported by name. Half a recording block is worse than none: it would
  apply an empty snapshot over live state.

`FindFirstFileA`/`FindNextFileA`/`FindClose` were the next stop and are
implemented in `src/native/kernel32.c`, with Windows' wildcard rules rather
than `fnmatch`'s -- `*.*` on Windows matches names with no dot, and a matcher
that dropped those would hand the asset scanner a silently shorter list.
`case_find_file` in the battery runs the matcher against BOTH classes (a
pattern that must hit, one that must not) on the real install.

**The run now holds a sustained 60fps frame loop**: 600 presents per 10s, 4
clears and ~18 draws per frame, for as long as it is left running (150s, ended
by the timeout, not by a fault).
