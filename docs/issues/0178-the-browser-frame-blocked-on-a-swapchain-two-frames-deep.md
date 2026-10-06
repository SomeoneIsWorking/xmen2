---
id: 178
title: The browser frame blocked on a swapchain only two frames deep
status: resolved
symptom: "every swapchain acquisition in the browser blocked, 58 ms a frame, on a renderer whose host draw recording was 2.65 ms"
state_items: S021
tags: web,browser,gpu,webgpu,swapchain,frame-rate
created: 2026-09-20
updated: 2026-09-20
---

# 0178 — the browser frame blocked on a swapchain only two frames deep

State item: S021 (web product)
Status: FIXED in this port (`kGpuFramesInFlight`).

## What was measured

The retail route in Zen (Firefox 156), now that it reaches gameplay (#177),
spent 58.3 ms of a 135.6 ms frame inside
`SDL_WaitAndAcquireGPUSwapchainTexture`, against 2.65 ms of host draw recording
and 1.10 ms of upload. A mean alone cannot say whether that is every frame
paying a fixed cost or a few frames stalling, so the frame timing now counts
the acquisitions that returned in under a millisecond — too short for a browser
turn, so they waited on nothing — and the longest single wait.

**12 of 1,603 acquisitions returned promptly.** 99.3% of frames blocked. It is
structural, not a stall.

## What it was

SDL's WebGPU backend refuses a swapchain texture while
`submittedCommandBufferCount >= maxFramesInFlight`, and its default depth is
two. In a browser the report that a submitted frame is done arrives about a
frame after the work itself finishes — it is delivered to the worker only
inside a WebGPU wait, and the presentation it is gated on belongs to the
compositor. With two slots the renderer is never more than one frame ahead, so
that report is always still outstanding when the next frame starts, and the
guest thread blocks for it.

Two other readings were tested and are **retired**:

- *The wait loop yields twice per frame.* It does: it re-acquires before
  reaping, so a slot freed by the reap is not noticed until after a second
  blocking wait. Reordering the loop in the SDL fork to reap first measured
  58.3 -> 52.1 ms a frame, which is inside this route's run-to-run spread. Both
  callbacks are delivered in the same browser turn, so the second wait was
  already free. The change was not landed.
- *The wait is a timeout slice expiring.* `WEBGPU_FENCE_WAIT_SLICE_NS` is 500
  ms and the observed waits are about 50 ms, so the waits end because the fence
  settles.

## The fix

`SDL_SetGPUAllowedFramesInFlight(device, kGpuFramesInFlight)` with
`kGpuFramesInFlight = 3`, set where the swapchain is claimed. A third frame in
flight gives the completion report a whole extra frame of the guest's own CPU
work to arrive in.

## What it bought, and where it stops

Measured on the same route, same driver, same host:

| frames in flight | frame wall | swapchain wait | acquisitions under 1 ms | frames in the run |
|---|---|---|---|---|
| 2 (SDL default) | 122.9 ms | 52.1 ms | 11 of 1,849 (0.6%) | 1,848 |
| **3** | **94.3 / 96.1 ms** | **40.4 / 36.4 ms** | **520 / 524 of ~2,400 (22%)** | **2,407 / 2,364** |
| 5 | 94.3 ms | 39.7 ms | 532 of 2,408 (22%) | 2,406 |

About **30% more frames**, and the two runs at depth 3 agree with each other.

Five is indistinguishable from three, so the depth is not what bounds it any
further: something else caps how far ahead this renderer can get — the surface's
own limit on unpresented textures is the obvious candidate and has not been
measured. 78% of acquisitions still block for about 36 ms, so most of that
latency is still being paid. Three is chosen over five because it is all of the
gain at the least added input latency.
