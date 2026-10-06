---
id: 129
title: Dead Zone sea lost its animated mipmapped multitexture material
status: resolved
symptom: The Dead Zone sea appears as fine repeating noise or a nearly flat blue fill instead of broad animated water
tags: pc,native,macos,arm64,gpu,d3d8,textures,water
created: 2026-08-27
updated: 2026-08-27
---

## Observation

A Dead Zone draw census falsified the earlier single-texture-stage claim. The
sea submits two animated stage-0 strips, plus environment-water strips that
select a constant stage 0 and modulate a second 2D texture using camera-space
normal coordinates and a COUNT2 texture transform. The animated stage-0 water
requests POINT minification with LINEAR mip filtering and has a complete
256x256-through-1x1 resident mip chain.

## Root cause

Three fixed-function contracts were missing. Stage 0 ignored its texture
matrix; stage 1 was absent; and every SDL sampler retained its zero-initialized
`max_lod`, which clamps sampling to level 0 even when D3D requests mip
filtering. Minified water therefore retained its finest repeating detail, and
the normal-mapped modulation pass never ran. The lowering now carries both
texture transforms and the evidenced stage-1 combiner/texgen state. Samplers
open the resident chain for POINT/LINEAR mip filtering and also preserve D3D's
LOD bias, min/mag filter, and anisotropy state.

The same investigation closed two fixed-function lighting mismatches exposed
by these packets: material diffuse/ambient/emissive sources now honor MATERIAL,
COLOR1, and COLOR2, and transformed normals are normalized only when
`D3DRS_NORMALIZENORMALS` requests it.

## Verification

The GPU multistage pixel selftest makes stage 0 and stage 1 select independently
known texels, then minifies a five-level texture: the no-mip control remains red
from level 0 while D3D mip filtering reaches the green 1x1 level. A bounded
Dead Zone run observes non-flat blue surface detail and frame-to-frame motion;
an isolated water-packet capture shows a coherent animated surface instead of
the reported screen-scale noise.

No stock Wine oracle was available on this macOS host, so this does not claim a
pixel-exact port-versus-stock comparison. The user's reference image owns the
visual target; the automated contracts cover the mechanisms that produced the
native defect.

The combined Linux verification after issue #135 exposed and corrected a
blind spot in the bounded observation: its original upper-right water crop was
hardcoded in 800x600 pixels because the then-unfixed fresh-profile path forced
that resolution. The case now scales the same region to the actual capture;
both the spawned-Critter and water-only cases must show non-flat blue detail
and frame-to-frame motion at the configured output size.
