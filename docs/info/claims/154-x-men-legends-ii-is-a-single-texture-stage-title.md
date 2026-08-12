---
id: C154
kind: claim
status: holds
created: 2026-08-12
tags: d3d8
---

## Claim

X-Men Legends II is a SINGLE texture stage title: no draw enables a texture stage beyond stage 0, so the host D3D8 reading stage 0 only costs nothing.

## Evidence

0 of 298,037 draws in a gameplay run (heartbeat line, scratch/logs). The counter is proved able to move: d3d8-selftest's multistage_counter_selftest drives a draw with D3DTSS_COLOROP on stage 1 and requires the number to change, so the zero is a measurement rather than an untested branch.

## What would falsify it

any run whose heartbeat reports a non-zero count on that line -- a later level, a different effect, or the Xbox build, which is a different renderer and is NOT covered by this
