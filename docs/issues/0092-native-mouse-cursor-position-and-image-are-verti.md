---
id: 92
title: Native mouse cursor position and image are vertically inverted
status: resolved
symptom: The native renderer mirrors the game cursor vertically: its Y position is reflected across the screen and the cursor sword image points up-right instead of the stock game's down-right.
tags: pc,native,graphics,d3d8,cursor,mouse,user-report
created: 2026-08-20
updated: 2026-08-20
---

## Root cause

The fixed-function shader converted pre-transformed `D3DFVF_XYZRHW` vertices
from D3D's top-left, Y-down pixel coordinates with the same increasing mapping
as X. SDL_GPU's clip convention needs the Y range reversed. The result mirrored
the complete cursor primitive: both its anchor position and its asymmetric
sword shape were reflected vertically. DirectInput's relative mouse Y was
already correct and was not changed.

## What was tried / dead ends

The existing D3D8 draw self-test looked at the centre of a symmetric triangle
and one untouched corner. Both pixels have the same values after a vertical
mirror, so that test passed while its claimed orientation guarantee was blind.
The old claim that XYZRHW and XYZ needed different cull mappings depended on
the same wrong Y conversion.

## Resolution

`d3d8_fixed.vert` now maps pixel Y to clip Y in reverse. The cull mapping was
updated with that coordinate correction, and the D3D8 pixel self-test now
checks two asymmetric pixels: the narrow upper-left must remain clear and the
wide lower-left base must be red. The old mirrored shader gives the opposite
answers.

Evidence:

- `scratch/build-native/x2native --no-window --d3d8-selftest`: all D3D8 GPU
  tests pass, including the asymmetric XYZRHW test and both cull conventions.
- `scratch/screenshots/cursor92-fixed-windowless.png`: real windowless tutorial
  frame, cursor at the top-left and sword pointing down-right, matching the
  stock orientation. Before the fix the same native path placed it bottom-left
  and pointed it up-right.

### Resolution (2026-08-20)
Corrected the D3DFVF_XYZRHW pixel-to-clip Y conversion, updated culling for the corrected coordinate convention, and added an asymmetric GPU readback test; a windowless real-game capture now matches the stock cursor orientation.
