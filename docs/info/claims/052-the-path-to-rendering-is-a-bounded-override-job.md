---
id: C052
kind: claim
status: falsified
created: 2026-08-05
tags: xbox
depends: vendor/xboxrecomp/src/d3d
falsified_on: 2026-08-05
---

## Claim

The path to rendering is a bounded override job, not open-ended RE: game code calls the statically-linked Xbox D3D8 library at exactly 73 distinct entry points across 215 call sites, and the toolkit already ships a host D3D8-to-OpenGL layer to route them to.

## Evidence

Cross-referencing tools/disasm/output/xrefs.json: calls from game .text (0x00011000-0x003EDB60) into each library section give D3D 215 sites / 73 distinct targets, XONLINE 147, DSOUND 124, XNET 66, XGRPH 49, D3DX 8. The two hottest D3D entry points (0x003F4160 x39, 0x003F06C0 x33) account for a third of the call sites. vendor/xboxrecomp/src/d3d implements IDirect3D8/IDirect3DDevice8 over OpenGL and exists for exactly this purpose.

## What would falsify it

if the title reaches D3D through function POINTERS rather than direct calls, the static xref count understates the surface -- the ICALL tally would show it

## FALSIFIED 2026-08-05

Overstated as THE path. The 73 D3D entry points and 215 call sites are correct, but overriding them is not what the toolkit intends and not what this title needs. The hottest of those entry points (0x003F06C0, 33 call sites) is a push-buffer command emitter -- it advances a pointer at 0x3FF940 against a limit at 0x3FF944 and writes command dwords -- so the title drives the NV2A directly rather than going through a replaceable COM API. The toolkit ships xemu's NV2A emulation (vendor/xboxrecomp/src/nv2a) precisely for that: a VEH hook decodes the faulting instruction at 0xFD000000+, the register write reaches the emulated GPU, and its PGRAPH translator turns push-buffer methods into calls on the toolkit's IDirect3DDevice8, which on Linux is OpenGL-backed. Our xbox/src/main.c had a TODO where that hook belongs and passed every GPU fault through, so no draw command could ever reach a GPU. That is now wired up.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
