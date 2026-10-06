---
id: C259
kind: claim
status: holds
created: 2026-08-24
tags: resolution,rmlui
depends: src/ui/rmlui_ui.cpp#sync_surface_metrics, src/presentation/aspect_fit.c#x2_aspect_fit
---

## Claim

Port Settings density follows the host output aspect-fit scale while the retail game keeps its 800x600 logical backbuffer

## Evidence

Clang aspect_fit test covers 1280x720, 1920x1080 and 3840x2160 design-space fits; a silent unbounded live HTTP capture returned a 1920x1080 final frame showing proportionally scaled Port Settings text over the retail composition.

## What would falsify it

A capture after changing output resolution shows Port Settings labels occupying a smaller fraction of the fitted output, or the guest logical CreateDevice dimensions stop being 800x600.
