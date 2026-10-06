---
id: C273
kind: claim
status: holds
created: 2026-08-27
tags: pc,native,macos,arm64,gpu,rendering,presentation
depends: src/gpu/gpu_device.c#pass_begin, src/gpu/gpu_frame_init_selftest.c#gpu_frame_init_selftest
---

## Claim

Untouched pixels in a new logical frame are opaque black, even when the game
does not issue a colour clear

## Evidence

Issue #124's three captures show recycled rectangular tiles specifically in
areas left outside the game draw, including a sparse loading frame where nearly
the whole target was undefined. `gpu_frame_init_selftest` first poisons the
production off-screen target blue, starts another frame with clear mask zero,
draws only a red triangle, and reads back black from both untouched edges while
the triangle remains red. It passes on the arm64 macOS Vulkan backend together
with the aspect-fit and mid-frame-clear pixel tests.

## What would falsify it

Any non-black pixel outside the area drawn by a no-colour-clear frame; the
frame-init test returning the blue poison, random attachment data, or an erased
triangle; or a late depth-only clear replacing rather than preserving colour
already drawn in the current frame.
