---
id: 95
title: FMV frames contain block-corrupted horizontal bands
status: open
symptom: CriMovie playback advances without hanging, but decoded story-movie frames contain blocky horizontal corruption across the lower half of the image
tags: pc,native,fmv,crimovie,graphics,corruption
created: 2026-08-21
updated: 2026-08-21
---

## Root cause


## What was tried / dead ends


## Resolution

### Note (2026-08-21)
Observed after the issue #79 queue-lifetime fix on a screenshot-confirmed story FMV at presented frame 20,210. The movie continued advancing at roughly 80-110 frames/s, so this is not a frozen/stale frame. scratch/screenshots/issue79-bounded-past.png shows correct upper image content with blocky horizontal corruption across the lower half. Upload lifetime is ruled out as the hang mechanism, not as the pixel mechanism: determine whether corruption already exists in the guest staging bytes before UnlockRect, then compare row pitch/dirty rectangle and the stock decoder output before changing the renderer.
