---
id: C052
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: vendor/xboxrecomp/src/d3d
---

## Claim

The path to rendering is a bounded override job, not open-ended RE: game code calls the statically-linked Xbox D3D8 library at exactly 73 distinct entry points across 215 call sites, and the toolkit already ships a host D3D8-to-OpenGL layer to route them to.

## Evidence

Cross-referencing tools/disasm/output/xrefs.json: calls from game .text (0x00011000-0x003EDB60) into each library section give D3D 215 sites / 73 distinct targets, XONLINE 147, DSOUND 124, XNET 66, XGRPH 49, D3DX 8. The two hottest D3D entry points (0x003F4160 x39, 0x003F06C0 x33) account for a third of the call sites. vendor/xboxrecomp/src/d3d implements IDirect3D8/IDirect3DDevice8 over OpenGL and exists for exactly this purpose.

## What would falsify it

if the title reaches D3D through function POINTERS rather than direct calls, the static xref count understates the surface -- the ICALL tally would show it
