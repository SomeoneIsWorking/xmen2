---
id: 182
title: every repeated upload allocated a new GPU transfer buffer, and the driver's address allocator became the frame
status: resolved
symptom: gameplay ran at 82 ms/frame with 85 ms of it inside upload staging; 308,362 uploads had asked the driver for 57,856 transfer buffers
state_items: S018,S006
tags: performance,gpu,upload,vulkan,android,sdl
created: 2026-09-22
updated: 2026-09-22
---

# 0182 — every repeated upload allocated a GPU buffer

State items: S018 (Android APK and measured mobile performance), S006 (renderer)

Reported as "performance needs optimizing", from a phone.

## What it was

SDL_GPU has no "write straight into a GPU resource": bytes go into a transfer
buffer and a copy pass moves them. Each resource kept its own transfer buffer
and each upload mapped it with cycling, which is required — SDL cycles a
resource the open command buffer has already referenced, and allocates when no
unbound generation is free. So a buffer uploaded twice in a frame paid for two
allocations, exactly as `gpu_upload_batch.h` had already written down when the
copies were batched.

The port's own instrument had been saying so for some time:

> `host upload 85.04 ms/frame (alloc 84.69 + record 0.35), 308362 uploads and 57856 transfer-buffer alloc(s)`

About 31 new GPU allocations every frame, for roughly 550 KB of actual data.
A `perf` profile of the Dead Zone map put **35.6% of all cycles in
`amdgpu_vamgr_find_va`** — the driver's virtual-address allocator — under
`radv_bo_create` ← `VULKAN_INTERNAL_CreateBuffer` ← `gpu_upload_stage`, with
another 5.4% in SDL's own memory-region bookkeeping. The frame was not moving
bytes; it was allocating address space.

## What it is now

`src/gpu/gpu_staging_ring.{h,c}` writes every upload at its own offset in a
shared 4 MiB page. SDL's rule is explicit that this needs no cycling at all:
*"It is OK to overwrite unreferenced data in a bound resource without
cycling."* Only reusing a page in a later frame overwrites referenced bytes,
so a page is cycled exactly once per frame, on its first write —
`gpu_upload_batch_flush` tells the ring when the frame's copies are submitted.
The destination resource is still cycled per upload, which is what keeps two
generations of one dynamic buffer alive within a frame.

Measured on the same map, same route:

| | before | after |
|---|---:|---:|
| frame wall | 88.4 ms | 49.6 ms |
| frame p50 | 82.0 ms | 48.6 ms |
| host upload | 85.04 ms/frame | 0.09 ms/frame |
| of which allocation | 84.69 | 0.05 |
| transfer-buffer allocations | 57,856, climbing ~31/frame | **1, for the whole run** |
| host draw | 5.1 ms/frame | 1.0 ms/frame |

Host draw fell with it: the allocator's lock was in the way of everything.

## What could have gone wrong and what proves it did not

A staging bug shows up as the wrong bytes in the right place, so the falsifier
is a picture: `tools/live_case.py deadzone-render` passes 6 of 6 with the sea
textured and changing, and all 15 GPU selftests pass — including the
upload-order one, which draws a dynamic buffer, rewrites it and draws again in
one frame and requires both generations to survive.

The reuse selftest now asserts the ring's contract over **two** frames, because
the interesting failure is in the second: a page whose used offset survives its
frame looks full the moment it is reused, and the ring would quietly allocate
another one every frame while its counter still read one. 128 uploads across
two frames share one page.

## What this does not claim

Desktop numbers are not Android performance evidence. What is portable is the
work removed — one allocation instead of tens of thousands — since the same
driver path is what an Android device pays for too. The named-device gate in
[android-release.md](../android-release.md) stays unmet.

The remaining 48.6 ms frame is now about 62% x87 softfloat emulation
(`x86p_x87_*` and `jit_x87_*` in the same profile), which is `shared/x86port`
work and open. The JIT also reports `0 of 0 x87 load(s) widened`, `0 of 0 x87
store(s) narrowed` and `0 of 0 SIMD instruction(s) emitted as host SIMD` on
this title, so those levers are dormant rather than exhausted.
