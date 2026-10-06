---
id: C005
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The PC build runs headless and unattended on this machine: Wine + the Lutris prefix's DXVK d3d8 + lavapipe software Vulkan + a Wine virtual desktop, rendering real frames to a capturable Xvfb. This is the oracle (milestone M2).

## Evidence

tools/run_shim.py stock 60 -> 800x600 frame with 1713 distinct colours showing the Beenox splash. Required all four of: native (DXVK) d3d8 because this Wine ships NO builtin d3d8; lavapipe ICD because Xvfb has no hardware Vulkan; virtual desktop because the game requests 800x600 FULLSCREEN and Xvfb cannot mode-switch; the Lutris prefix (WINE_PREFIX) because a bare wineboot prefix lacks d3d8 entirely.

## What would falsify it

A Wine, DXVK, or Mesa upgrade changing any of those four dependencies would break it; so would the game reaching a point past the splash that needs input or a real GPU.
