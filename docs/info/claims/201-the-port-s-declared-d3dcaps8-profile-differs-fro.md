---
id: C201
kind: claim
status: holds
created: 2026-08-15
tags: d3d8,caps,graphics
---

## Claim

The port's declared D3DCAPS8 profile differs from the real driver's in 15 of 53 fields, measured for the first time -- but none of the differing fields selects a geometry path, so the caps are NOT the warped-geometry cause.

## Evidence

tools/proxy_d3d8 now wraps IDirect3D8::GetDeviceCaps (slot 13) and IDirect3DDevice8::GetDeviceCaps (slot 7) and dumps D3DCAPS8 in the same words src/d3d8/d3d8_caps.c does, from one shared field list (src/d3d8/d3d8_caps_fields.h), so the two are diffable line for line. Port scratch/logs/portcaps.log vs stock scratch/run/stocklog/d3d8_lightlog.txt: identical in 38 fields; differing in Caps2, PresentationIntervals, DevCaps, RasterCaps (port claims D3DPRASTERCAPS_WBUFFER the backend has no W-buffer for), TextureCaps (stock has PROJECTED+TEXREPEATNOTSCALEDBYSIZE, port has NONPOW2CONDITIONAL), MaxTextureWidth/Height (0x1000 vs 0x4000), MaxVolumeExtent, MaxTextureAspectRatio, GuardBand{Left,Top,Right,Bottom} (0 vs +/-32768), MaxPointSize (64 vs 256), MaxPrimitiveCount, MaxStreamStride (256 vs 508), MaxVertexShaderConst (96 vs 256), PixelShaderVersion (1.1 vs 1.4), MaxPixelShaderValue. MaxVertexBlendMatrices, MaxVertexBlendMatrixIndex, VertexProcessingCaps, MaxActiveLights, FVFCaps and VertexShaderVersion are IDENTICAL, which is why no vertex-path choice can differ.

## What would falsify it

an engine path that reads one of the 15 differing fields and changes what geometry it submits -- or a run in which aligning any of them changes the rendered shape
