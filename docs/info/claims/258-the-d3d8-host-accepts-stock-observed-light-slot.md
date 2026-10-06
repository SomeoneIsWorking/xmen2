---
id: C258
kind: claim
status: holds
created: 2026-08-24
tags: d3d8,lighting
depends: src/d3d8/d3d8_light_selftest.c#d3d8_light_selftest, src/d3d8/d3d8_drawcall.c#d3d8_build_draw
---

## Claim

The D3D8 host accepts stock-observed light slot 51 through SetLight, LightEnable, and fixed-function draw translation

## Evidence

The d3d8_host selftest drives slot 51 through the production device vtable, verifies retained diffuse state, then renders the existing flipped-normal pixel discriminator with slot 51. A compile-time assertion prevents the table from shrinking below the observed index.

## What would falsify it

SetLight or LightEnable rejects slot 51, the draw path omits its enabled light, or the fixed-function pixel discriminator fails.
