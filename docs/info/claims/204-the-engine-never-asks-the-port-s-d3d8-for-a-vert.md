---
id: C204
kind: claim
status: holds
created: 2026-08-15
tags: d3d8,vertex-shader,skinning,caps
---

## Claim

The engine never asks the PORT's D3D8 for a vertex shader at all: 9 distinct SetVertexShader values in a driven gameplay run, every one an FVF code, 0 with D3DFVF_RESERVED0 set. The divergence from the control is therefore UPSTREAM of D3D8, not inside it.

## Evidence

scratch/logs/native.log, a run driven by hand into the first dialog: the shutdown census lists 0x142 x3319, 0x144 x444, 0x042 x2616, 0x102 x1459, 0x012 x139, 0x002 x897, 0x112 x2695, 0x152 x562, 0x052 -- nine FVF codes, none with bit 0 set -- beside '0 vertex shader(s) created, 0 deleted, 0 still live; 0 shader-lifecycle call(s) refused'. The control, through tools/proxy_d3d8, binds handle 0x003 (bit 0 = D3DFVF_RESERVED0 = a shader handle) with a 32-byte vertex for draws 29 and 31 of every gameplay frame (C203). cg.dll and cgD3D8.dll ARE mapped and their entry points ran in the port (native.log lines 61-87), so the Cg modules loading is not the difference. This also makes d3d8_drawcall.c's  test -- which is not D3D8's bit-0 rule -- LATENT rather than the cause: it is never reached with a handle.

## What would falsify it

a run in which the port's SetVertexShader census shows a value with bit 0 set, or evidence that the engine's shader decision is made after a D3D8 call this census does not cover
