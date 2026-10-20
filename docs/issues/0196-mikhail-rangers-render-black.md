---
id: 196
title: Mikhail Rangers render as black silhouettes
status: resolved
symptom: a Mikhail Ranger draws solid black while the party beside it is lit and textured; the user reports two more enemy types doing the same
state_items:
tags: pc,native,gpu,d3d8,rendering
created: 2026-10-20
updated: 2026-10-20
---

# 0196 — Mikhail Rangers render as black silhouettes

## Reproduction

`tools/live_case.py ranger-render`: Continue the autosave, `loadmap
act1/deadzone/deadzone2`, spawn `_ms_ss_mikhailranger_a` 120 units from the
active hero through `/script`. The shot `enemy.png` shows the Ranger as a
black silhouette beside a normally lit party; the renderer refuses nothing.

Booting with `X2_BOOT_MAP=act1/deadzone/deadzone2` draws the whole party
black the same way, while the Continue route draws them normally.

## Cause

`src/d3d8/d3d8_vs_decode.cpp:decode_program` did not implement `m3x3`
(opcode 23) or `max` (opcode 11), so it refused every draw of vertex shader
0xf0000202, the skinned program that lights a character from c5-c11 and
writes oD0 and oT0. Only the untextured outline passes (0xf0000101,
0xf0000303) drew, which is a solid black silhouette. The party used the same
program on the `X2_BOOT_MAP` route.

The frame table could not show this: it printed "NO host-readable vertices"
for every programmable draw. It now runs the program on the CPU executor and
reports the shader handle, screen rectangle, texture and diffuse range.

## Fix

The decoder expands `m4x4`/`m4x3`/`m3x4`/`m3x3`/`m3x2` into one `dp4` or
`dp3` per written component against consecutive constants, relative or not;
`min` and `max` are new ops in the CPU executor and the GPU program.

## Verification

- `ranger-render`: the frame table shows draw 233 as VS 0xf0000202, texture
  787, diffuse luma 0.000..1.781, at the Ranger's position; the shot shows a
  lit red-armoured Ranger, and no draw is refused.
- `X2_BOOT_MAP=act1/deadzone/deadzone2`: the pre-fix build draws all four
  heroes black and logs `VS 1.1 opcode 23 ... the draw is refused`; the
  fixed build draws them lit and logs no refusal.

## The other shipped programs

`libIGGfx.dll` and `libIGSg.dll` embed 32 VS 1.1 programs. The decoder now
accepts every instruction they use, including `rcp`, `rsq` and `dst`, which
only the point-light variants of the skinned program need.

- `igDxVertexBlendingShaderManager::setup` (`libIGGfx` 0x10038f80) registers
  the variants by directional and point light count; `findShaderHandle` picks
  one from the draw's enabled lights and the bone palette size.
- The light limits come from `alchemy.ini` `[GFX]` `maxVertexBlend*Lights`, or
  from `adjustLoadBalancing`, which weighs `igGetCPUCaps` against GPU ratings,
  so which characters take the vertex shader path depends on the machine.
- Characters only ever get directional lights (#3 and a per-character pair);
  point lights only reach environment draws. With
  `maxVertexBlendPointLights=4` a Ranger beside a Dead Zone 2 fire still used
  the directional program, so no point-light variant was seen running.
- The two `libIGSg` programs that write `oT1` belong to `igCartoonShader`,
  which no game file uses.
