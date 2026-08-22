---
id: 106
title: Retail shadows are procedural decals; light-cast dynamic shadows need explicit enhancement policy
status: investigating
symptom: The native renderer has no light-cast shadow-map pass, but adding one cannot be justified as restoration of the retail DetailedShadow option
tags: pc,native,graphics,shadows,d3d8,architecture
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

`DetailedShadow` is a dead persisted display setting in this build. Its backing
byte at `0x00a68d40` has exactly four incoming references in `XMen2.exe`:
default initialisation, registry load, registry save, and a secondary settings
reload used by options processing. The retail page does not expose a
corresponding control. No gameplay, render, or `CShadowMgr` path reads it.
Forcing the value
therefore cannot select a different renderer route.

The title-specific owner is `CShadowMgr`, not the three named Alchemy shadow
shader classes. Its render callback (`FUN_004b64e0`) walks a 32-entry pool and,
for flag bit zero, calls `FUN_004b5700`. That helper emits six black, alpha-
modulated vertices oriented from per-entity position/direction data. The result
continues through the ordinary Alchemy geometry/sorter/render-package path. It
does not select a light, render caster depth, classify receiver geometry, or
allocate an offscreen target.

## Native boundary

Existing native boundary traces contain both functions (`FUN_004b64e0` six
times and `FUN_004b5700` sixteen times in one level trace), followed by the
ordinary geometry build path. Static RE identifies each helper result exactly:
one non-indexed six-vertex triangle fan, with XYZ/diffuse/UV at stride 24.
Matched stock frames contain five such four-primitive fans at the same draw
ordinals for `DetailedShadow` OFF and ON. Existing native frame dumps contain
the same draw family after the renderer's exact fan-to-triangle-list expansion,
with the same texture/combiner, blend, depth, cull, lighting, layout, bias,
stencil, and colour-write vocabulary. The producer is therefore present and
submitted by native; no renderer divergence has been observed at this boundary.

`GpuDraw` has resolved geometry, transforms, lights, and fixed-function state,
but no stable scene identity, caster/receiver classification, bounds, or chosen
shadow light. The current offscreen helper is a global readback-oriented test
target, not a first-class sampleable render resource. The D3D8 facade also has
no `CreateRenderTarget`, `CreateDepthStencilSurface`, `CopyRects`, or
`UpdateTexture` implementation. None of those omissions breaks the evidenced
retail decal producer, which submits ordinary geometry.

## Correct next steps

For retail parity, the remaining high-value check is dynamic object-to-draw
identity and matching vertex bytes in one same-scene stock/native capture. Fix
the first divergent boundary only if one is observed; the current evidence does
not justify a renderer change.

For the requested light-cast dynamic-shadow enhancement, first choose and
document caster, receiver, light-selection, projection/frustum, resolution,
update, bias, and filtering policy. Then add a narrow scene-to-renderer frame
input and first-class sampleable depth resources. The pass belongs inside the
logical D3D scene before `gpu_present_composite`; aspect fitting and RmlUi stay
downstream. See `docs/RE/shadows.md`.

## Falsifier

A gameplay/render reference to `0x00a68d40`, a positive retail observation of
another title route, or evidence that `FUN_004b5700` performs a light/depth-
target pass would invalidate this account.
