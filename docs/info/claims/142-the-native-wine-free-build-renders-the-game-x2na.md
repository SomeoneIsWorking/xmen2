---
id: C142
kind: claim
status: holds
created: 2026-08-07
tags: pc,native,graphics,d3d8,milestone
---

## Claim

The native (Wine-free) build RENDERS the game: x2native --d3d8 --run reaches a sustained 60fps loop and draws the game's own save/load panel through src/d3d8 -> src/gpu on SDL3+Vulkan.

## Evidence

scratch/screenshots/native.png, captured by tools/native_shot.py on Xvfb at 800x600: the panel with its gradient fill, bevelled border, bottom tabs and cursor, 1574 distinct colours, black 53.4% (the area outside the window). The heartbeat over the same run: 600 presents per 10s, 4 clears and ~18 draws per frame, and gpu draws == the engine's draw count with 0 refused, so every draw the engine asked for was rasterised. The run ended by the timeout, not by a fault.

## What would falsify it

a native_shot.sh capture that comes back one flat colour, or a heartbeat line where gpu draws stops rising while the engine's count keeps going
