---
id: 79
title: Windowless FMV playback exhausts GPU upload allocations and stalls
status: resolved
symptom: A windowless unbounded story movie slows from hundreds of frames per second to multi-second frames and appears frozen while the control server and guest time continue advancing.
tags: pc,native,fmv,crimovie,hang,gpu,headless
created: 2026-08-15
updated: 2026-08-21
---

## Decoder ruled out

The original reading blamed `libCriMovie`'s 591-instruction decoder at
`0x10040000`. Scoped recording of seven loop checkpoints captured 200,000
entries over 79 calls; checkpoint `0x100400dd` had 52,397 distinct
register/input-pointer states. The decoder was consuming input rather than
spinning on an unchanged condition.

During a reproduced multi-second frame stall, gdb instead caught the only busy
host thread inside Vulkan/AMDGPU allocation, reached from the SDL GPU upload
path. The heartbeat supplied the denominator: 23,634 uploads had created
exactly 23,634 transfer-buffer objects, and the longest frame took 42.3 seconds.

## Root cause

Two resource-lifetime errors combined:

- every upload created and immediately retired an SDL GPU transfer buffer;
- the headless target had no swapchain acquisition, so nothing bounded how
  many frames an unbounded run could enqueue ahead of the GPU.

The queue accumulated pending backing allocations until buffer creation and
mapping stalled for seconds. It looked like a movie-decoder hang because the
movie is the first sustained stream of per-frame texture uploads.

### Resolution (2026-08-21)
Root cause: the windowless renderer created and retired one SDL_GPU transfer buffer per upload, then submitted headless frames without the swapchain backpressure that bounds visible frames. Unbounded movie playback accumulated pending Vulkan allocations until transfer mapping/allocation stalled for seconds and looked like a CriMovie spin. GPU resources now retain and cycle one staging transfer buffer, and headless frame submission waits on its completion fence. The screenshot-confirmed story-FMV stress replay passed frame 20,210 with uploads remaining bounded and no stall.

The fixed replay remained at roughly 80–110 frames/s through the old failure
point: 49,868 uploads used 525 retained transfer allocations at about 0.08 ms
of upload time per frame. The movie's block-corrupted pixels are independent
issue #95.
