---
id: C169
kind: claim
status: falsified
created: 2026-08-12
tags: renderer
falsified_on: 2026-08-12
---

## Claim

The menu's sky dome is drawn UNTEXTURED, UNLIT and with no diffuse (draw 1 of the menu frame: 16-triangle strip, stride 12, position only), so D3D8's own fixed-function rules render it WHITE. The white sky is therefore not evidence of a missing feature in this backend, and the stored explanation that it is caused by unimplemented cube sampling is stale.

## Evidence

X2_FRAME_DUMP=busy:150 on the native --d3d8 build, frame 2239: 'draw 1 tristrip x16 tex 0 UNTEXTURED ztest zwrite unlit nonorm stride 12 col -1 uv -1'. The same run reports 296 'add (environment map)' draws, so cube sampling is working. Breakdown of the frame: 99 lit-norm, 128 unlit-nonorm, 5 unlit-norm, 0 lit-nonorm.

## What would falsify it

a stock (Wine) capture of the same menu camera position showing a non-white sky -- that would mean something else colours it and this backend is missing that draw

## FALSIFIED 2026-08-12

FALSIFIED by a reference capture of the shipped game (Polish retail build, 1024x768, found by the user): the main menu sky is a SUNSET GRADIENT -- deep purple-blue at the top through mauve to warm orange at the horizon, with cloud banding -- and the whole scene is warmer and brighter than this port renders it. That is precisely the observation this claim named as its falsifier. So the white sky IS a defect: something colours that sky in the real game and this backend is not producing it. The reasoning that led here was sound but incomplete -- the menu's draw 1 really is an untextured, unlit, stride-12 dome, and D3D8 really would render THAT white -- which means the sky is NOT draw 1, and the draw that paints it is either missing, dropped, or being overdrawn. Look for it rather than assuming the dome is the sky.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
