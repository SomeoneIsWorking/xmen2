---
id: C234
kind: claim
status: holds
created: 2026-08-22
tags: d3d8,gpu,characters
depends: src/gpu/gpu_draw.c#upload_bytes, src/gpu/gpu_upload_selftest.c#gpu_upload_order_selftest, src/d3d8/d3d8_resource.c#buf_Unlock
---

## Claim

SDL_GPU buffer cycling preserves distinct D3D8 dynamic-buffer generations for draws recorded before and after a same-frame rewrite

## Evidence

gpu_upload_order_selftest failed on the old upload path with the first draw still clear-blue while the second was green, then passed red-left/green-right after gpu_buffer_upload enabled destination cycling. Real gameplay logs soldier_test3/4/5 exercise 294/1340/324 write-after-draw generations, establishing that this is a shipping path rather than a synthetic-only case.

## What would falsify it

the upload-order selftest fails, SDL documents cycling with different semantics, or the reported soldier scene still shows a prior draw consuming a later actor buffer generation
