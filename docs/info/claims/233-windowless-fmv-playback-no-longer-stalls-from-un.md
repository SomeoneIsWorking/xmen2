---
id: C233
kind: claim
status: holds
created: 2026-08-21
tags: gpu,fmv,headless
depends: src/gpu/gpu_upload.c#gpu_upload_stage, src/gpu/gpu_frame_submit.c#gpu_frame_submit
---

## Claim

Windowless FMV playback no longer stalls from unbounded GPU upload allocation: resources retain cycled staging storage and headless frames apply completion backpressure

## Evidence

Issue #79 before/after on the screenshot-confirmed New Game -> Normal story-FMV route. Before: 23,634 uploads/23,634 transfer allocations, gdb in AMDGPU allocation, 42.3 s max frame. Scoped decoder trace: 200,000 entries with 52,397 distinct loop states. After: passed frame 20,210 at roughly 80-110 frames/s, 49,868 uploads/525 allocations, about 0.08 ms upload time/frame. gpu_upload_reuse_selftest independently requires two uploads to use one allocation.

## What would falsify it

A screenshot-confirmed windowless unbounded story-FMV replay again stops presenting for 20 consecutive one-second samples while retained transfer allocations remain bounded, or two uploads of one unchanged resource create more than one transfer allocation
