---
id: C202
kind: claim
status: falsified
created: 2026-08-15
tags: d3d8,gpu,geometry
falsified_on: 2026-08-22
---

## Claim

The warped geometry is NOT caused by dynamic-buffer upload ordering: no buffer is ever rewritten in a frame that already drew from it.

## Evidence

src/d3d8/d3d8_resource.c marks every vertex and index buffer at draw time (d3d8_resource_note_drawn, called from fill_request) and buf_Unlock counts an unlock whose buffer was drawn from in the SAME presented frame. That is the exact hazard: gpu_buffer_upload (src/gpu/gpu_draw.c:207-222) acquires its own command buffer and submits it immediately while the frame's command buffer stays open until Present (src/gpu/gpu_device.c:676), so a mid-frame upload reaches the GPU BEFORE every draw of that frame. Measured in a run driven into gameplay by hand (scratch/logs/f9probe.log): 12994 unlocks, 12762 of them D3DLOCK_DISCARD, 457 MB moved -- and 0 rewrote a buffer this frame's draws had already read, 0 relocked within one frame. The earlier counter (relocked-in-one-frame) did not answer this and was replaced.

## What would falsify it

a run in which the hazard counter in the heartbeat is non-zero, or a scene whose warping changes when uploads are made to ride the frame's own command buffer

## FALSIFIED 2026-08-22

The old counter reached 294 write-after-draw unlocks in scratch/logs/soldier_test3.log and 324 in soldier_test5.log. The original zero-result run never exercised the dynamic actor-buffer reuse seen in the reported scene.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
