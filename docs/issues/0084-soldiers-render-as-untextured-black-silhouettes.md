---
id: 84
title: Soldiers render as untextured black silhouettes, and one lies in a dead pose while still fighting
status: resolved
symptom: an enemy soldier draws as a solid black silhouette with its weapon fully textured, and another soldier lies flat on the floor in a dead-looking pose during a live fight
tags: pc,native,graphics,d3d8,textures,animation,characters,user-report
created: 2026-08-19
updated: 2026-08-22
---

REPORTED BY THE USER, 2026-08-19, from a real play session. Root-caused and
fixed on 2026-08-22.

Capture: `scratch/shots/report-2026-08-19-black-soldier.png` (gitignored -- it
is a frame of the shipped game).

## What is in the frame

* A standing soldier is a **solid black silhouette** -- no diffuse at all -- while
  the RIFLE HE HOLDS is fully textured and lit, and his muzzle flash draws
  correctly. So this is per-mesh or per-material, not a whole-draw or whole-frame
  failure, and it is not the lighting path being off for the scene.
* A second soldier lies **flat on the floor in a dead/prone pose** in the middle
  of a live firefight.
* The player character (Wolverine) beside them is textured and shaded correctly,
  and so is the level geometry, so the failure is confined to some subset of
  the character draws.

The user's words: "soldiers sometimes look like this (body on the floor like
dead, untextured black guy shooting)". SOMETIMES is the important word -- the
same enemy type draws correctly elsewhere in the same level, so whatever
selects the failing case is intermittent.

## Root cause

`gpu_buffer_upload` submitted a rewrite immediately while the frame command
buffer still held earlier draws against the same `SDL_GPUBuffer`. D3D8 dynamic
actor buffers use draw -> DISCARD/rewrite -> draw. Without destination cycling,
every recorded draw consumed the last actor generation. That explains both the
black body mesh and wrong/prone actor geometry while the separately buffered
weapon remained correct.

## Verification

The focused `gpu_upload_order_selftest` failed on the old path: its first red
draw disappeared while only the later green draw survived. It passes after
`SDL_UploadToGPUBuffer` enables destination cycling. Real gameplay logs exercise
294 to 1,340 same-frame write-after-draw generations, proving the ordering is a
shipping path rather than a synthetic-only case (C234).

The exact intermittent soldier frame was not recaptured after the fix. That is
an observational limit, not a reason to replay a non-deterministic encounter
after the causal ordering test and real-path counters have answered the defect.
