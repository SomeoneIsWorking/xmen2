---
id: C154
kind: claim
status: falsified
created: 2026-08-12
tags: d3d8
falsified_on: 2026-08-27
---

## Claim

X-Men Legends II is a SINGLE texture stage title: no draw enables a texture stage beyond stage 0, so the host D3D8 reading stage 0 only costs nothing.

## Evidence

0 of 298,037 draws in a gameplay run (heartbeat line, scratch/logs). The counter is proved able to move: d3d8-selftest's multistage_counter_selftest drives a draw with D3DTSS_COLOROP on stage 1 and requires the number to change, so the zero is a measurement rather than an untested branch.

## What would falsify it

any run whose heartbeat reports a non-zero count on that line -- a later level, a different effect, or the Xbox build, which is a different renderer and is NOT covered by this

## FALSIFIED 2026-08-27

Dead Zone live run: its environment-water passes bind a second 2D texture, MODULATE/MODULATE it with CURRENT, generate coordinates from the camera-space normal, and apply a COUNT2 texture transform. The old 0/298,037 observation covered a different gameplay route, exactly the later-level falsifier named by the claim.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
