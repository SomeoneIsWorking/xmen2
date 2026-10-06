---
id: 64
title: Programmable D3D8 vertex shaders block post-conversation gameplay
status: resolved
symptom: After the opening Cyclops conversation ends, native gameplay aborts because IDirect3DDevice8::CreateVertexShader and SetVertexShaderConstant are unimplemented.
tags: d3d8,vertex-shader,gameplay,engine-port
created: 2026-08-13
updated: 2026-08-14
---

## Observation

The deterministic dense route
`f2400-2900/60:Return,f2820-2900/20:Return` changes conversation flags
0x13 → 0x18, then calls `CreateVertexShader` with a five-token declaration
and VS 1.1 bytecode. The declaration is
`20000000 40020000 40030001 40040002 ffffffff`; the shader is 104 DWORDs
and ends in `0000ffff`. The next call is
`SetVertexShaderConstant(1, 0x04673960, 4)`. Evidence:
`scratch/logs/create-vs-dump.log`.

## Root cause

The native D3D8 host implemented only fixed-function FVF vertex processing.
Alchemy switches to a real programmable VS 1.1 skinning path when the level
begins its next gameplay phase, so slots 75 and 79 were absent and the draw
builder refused its shader handle.

## Resolution

The host now owns generation-checked D3D8 shader handles, copies and returns
the original declaration/function streams, mirrors all 96 constant registers,
and implements create/bind/get/delete/query. The draw path interprets VS 1.1
over the host-visible D3D8 staging bytes and uploads its actual clip-position,
diffuse and texture-coordinate outputs. It supports the observed MOV, ADD, SUB,
MUL, MAD, DP3 and DP4 instructions, masks, swizzles and relative constant
addressing; any other opcode refuses by name.

`DrawPrimitiveUP`, reached immediately afterward, now copies its transient
bytes as D3D8 requires and shares the same draw path instead of becoming a
parallel renderer.

Verification on 2026-08-14:

- `--d3d8-selftest` executes a relative-addressed DP4 program with four
  distinct expected components and proves the shipping executor rejects an
  unsupported opcode.
- The real route reached frame 3350: 206,586 draws, zero GPU refusals, one real
  104-DWORD shader, 50 programmable draws and 3,250 vertex invocations.
- The captured filmstrip shows post-conversation gameplay and moving party
  geometry. Characters remain black because issue #62's upstream light
  diffuse is zero; that is separate from shader creation or execution.

Evidence: `scratch/logs/vs-execute-3350.log` and
`scratch/screenshots/vs-3350-contact.png`.

### Resolution (2026-08-14)
The host now implements generation-checked shader lifecycle, the observed VS 1.1 skinning interpreter, constant registers, and DrawPrimitiveUP. The frame-3350 route executes 50 shader draws / 3250 vertices with zero GPU refusals; unsupported tokens remain loud.

### Note (2026-08-14)
Post-cull-fix concern about detached black geometry at the upper-left was tested over 12 consecutive X2_SHOT_VS frames. It is Nightcrawler's teleport animation: the same skinned mesh moves continuously from the ceiling into a crouch, with 40 programmable draws / 2600 vertices and zero refusals. It is not detached geometry and not evidence of a VS transform fault. Evidence: scratch/screenshots/vs-sequence-contact.png and scratch/logs/vs-sequence.log.
