---
id: C169
kind: claim
status: holds
created: 2026-08-12
tags: renderer
---

## Claim

The menu's sky dome is drawn UNTEXTURED, UNLIT and with no diffuse (draw 1 of the menu frame: 16-triangle strip, stride 12, position only), so D3D8's own fixed-function rules render it WHITE. The white sky is therefore not evidence of a missing feature in this backend, and the stored explanation that it is caused by unimplemented cube sampling is stale.

## Evidence

X2_FRAME_DUMP=busy:150 on the native --d3d8 build, frame 2239: 'draw 1 tristrip x16 tex 0 UNTEXTURED ztest zwrite unlit nonorm stride 12 col -1 uv -1'. The same run reports 296 'add (environment map)' draws, so cube sampling is working. Breakdown of the frame: 99 lit-norm, 128 unlit-nonorm, 5 unlit-norm, 0 lit-nonorm.

## What would falsify it

a stock (Wine) capture of the same menu camera position showing a non-white sky -- that would mean something else colours it and this backend is missing that draw
