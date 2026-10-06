---
id: 124
title: Untouched logical-backbuffer pixels showed recycled GPU tile memory
status: resolved
symptom: Side and out-of-bounds areas show tiled scene fragments, and sparse loading frames sometimes become full-window noise
tags: pc,native,macos,arm64,gpu,rendering,presentation
created: 2026-08-27
updated: 2026-08-27
---

## Observation

Three arm64 macOS captures showed the same defect at different coverage: a
4:3 legal screen had fragments and text repeated down both sides; a gameplay
view had rectangular scene fragments above the area the game drew; and one
loading frame was almost entirely random RGB noise with only sparse loading
text and cursor draws intact. The final aspect-fit compositor already cleared
its destination to black and its pixel self-test passed, so those captures
falsify the initial diagnosis that the compositor's pillarbox clear was absent.

## Root cause

`gpu_device.c::pass_begin` used `SDL_GPU_LOADOP_DONT_CARE` for the first colour
pass when the game had not called `Clear`. The persistent logical scene target
is larger than some game viewports, and loading/UI frames can touch only a
small fraction of it. DONT_CARE does not define untouched pixels; Metal/Vulkan
were free to expose recycled attachment tiles, which the compositor then
faithfully copied into the window.

Loading the old target is not correct either: D3D's discarded back buffer does
not promise prior-frame contents. The first pass now clears to opaque black
when the game supplies no colour clear. Explicit game clears retain their
colour, and a mid-frame pass reopened for a depth-only clear still LOADs colour
so current-frame geometry survives.

## Verification

`gpu_frame_init_selftest` poisons all 64x64 pixels blue, begins a second logical
frame without a game colour clear, draws only a red triangle, and reads the
production target back. The triangle is red and two untouched edge samples are
both `0xff000000`. The arm64 `vk_frame_path`, `d3d8_host`, and `aspect_fit`
tests pass; the existing mid-frame clear test also continues to prove that a
depth-only reopen preserves current-frame colour.
