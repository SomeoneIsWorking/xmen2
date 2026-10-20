---
id: 128
title: Scourge Critter rendered black because R8G8B8 was not a host texture format
status: resolved
symptom: Scourge Critter spiders appear as black silhouettes while most enemies render normally
tags: pc,native,macos,arm64,gpu,d3d8,textures
created: 2026-08-27
updated: 2026-10-20
---

## Observation

The defect reproduces without walking to an authored encounter:
`tools/live_case.py deadzone-render` Continues into Dead Zone 1 and spawns
`_ms_ss_critter_a` beside the party through `/script` (see
[Console](../RE/console.md)). The run used to report D3D format 20
(`D3DFMT_R8G8B8`) as unsupported.

## Root cause

The D3D8 resource layer had no representation for the 24-bit R8G8B8 texture
used by this actor. The GPU path now records it as three-byte BGR source data
and expands each upload to opaque BGRA8, which SDL_GPU can sample directly.
Pitch and luma diagnostics use the source's actual three bytes per pixel.

## Verification

`test_gpu_texture_format` checks the BGR-to-BGRA expansion byte for byte. The
bounded `deadzone-render` live case shows the spawned entity in `/entities`
and the spawn trace, the Critter model requested and nothing refused; its
captured Scourge Critter is textured brown instead of black.
