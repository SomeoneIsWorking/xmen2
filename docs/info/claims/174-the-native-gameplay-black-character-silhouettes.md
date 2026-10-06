---
id: C174
kind: claim
status: holds
created: 2026-08-14
tags: d3d8,rendering,cull
---

## Claim

The native gameplay black-character silhouettes were caused by applying the XYZRHW cull translation to model-space XYZ draws; model-space and pretransformed draws require opposite SDL cull mappings.

## Evidence

src/gpu/gpu_draw.c PipeKey.pretransformed and pipeline_for cull mapping; src/d3d8/d3d8_report.c d3d8_draw_selftest. --d3d8-selftest passes both position conventions; deliberate old model-space mapping fails with centre 0xff00ff00. scratch/screenshots/cull-fixed-full.png shows textured/lit characters and scratch/logs/cull-fixed-full.log records 110188 submitted, 0 refused.

## What would falsify it

A matched stock capture shows those character passes are not back-face outline hulls, or a backend/viewport change alters winding without making either pixel self-test fail.
