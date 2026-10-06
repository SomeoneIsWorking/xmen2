---
id: C228
kind: claim
status: holds
created: 2026-08-20
tags: graphics,d3d8,cursor,mouse
depends: src/gpu/shaders/d3d8_fixed.vert#main, src/gpu/gpu_draw.c#pipeline_for, src/d3d8/d3d8_report.c#d3d8_draw_selftest, src/d3d8/d3d8_screen_space_test.c#d3d8_screen_space_pixels_check
---

## Claim

The native cursor's vertical position and image orientation match the stock game because XYZRHW pixel Y is converted with the required reversed clip-space range

## Evidence

Before the change, the same native path captured the cursor at bottom-left pointing up-right while the stock cursor points down-right. After reversing XYZRHW clip Y, scratch/screenshots/cursor92-fixed-windowless.png shows the native cursor top-left pointing down-right. The asymmetric D3D8 readback test requires upper-left clear and lower-left red; all --no-window --d3d8-selftest cases pass.

## What would falsify it

a stock/native capture driven to the same cursor coordinates differs in vertical position or image orientation, or the asymmetric XYZRHW readback test accepts a shader that maps Y like X
