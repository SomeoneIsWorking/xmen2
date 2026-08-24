---
id: C260
kind: claim
status: holds
created: 2026-08-24
tags: control,screenshot,gpu
depends: src/native/control.c#control_frame_pump, src/gpu/gpu_capture.c#gpu_capture_submit_frame, src/gpu/gpu_device.c#gpu_frame_end
---

## Claim

Windowed HTTP screenshots complete at the render boundary without waiting for a guest input poll

## Evidence

The old live path returned 504 while frames advanced; after moving CMD_SHOT to final-frame completion, the same silent unbounded 1920x1080 run returned a 6222418-byte PNG in 1.2 seconds at frame 65 before input polling. control_screenshot, window_capture_wiring, vk_frame_path and d3d8_host passed.

## What would falsify it

A presenting run with no guest input polls times out or returns a frame different from the final aspect-fit plus RmlUi composition.
