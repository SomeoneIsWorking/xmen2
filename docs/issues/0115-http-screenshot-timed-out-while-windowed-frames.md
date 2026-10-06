---
id: 115
title: HTTP screenshot timed out while windowed frames were still presenting
status: resolved
symptom: The windowed control channel returned 504 for /screenshot during loading and FMVs even though /status showed the frame counter advancing.
tags: control,screenshot,gpu,http
created: 2026-08-24
updated: 2026-08-24
---

## Root cause

CMD_SHOT was drained only by control_pump, whose ownership boundary is the guest input poll. Loading and FMV presentation can advance hundreds of frames without polling input, so the screenshot was never armed even though the renderer was healthy.

## Resolution

Screenshot requests are now drained by a render-thread observer after final-frame submission and readback. Guest input, save, and assignment commands remain on the guest-input boundary. The existing control mutex and condition variable provide bounded request ownership. A silent unbounded 1920x1080 live run returned the PNG in 1.2 seconds at frame 65, before guest input polling, where the previous path timed out after 10 seconds.
