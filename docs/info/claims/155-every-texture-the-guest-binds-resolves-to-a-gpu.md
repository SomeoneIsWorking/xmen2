---
id: C155
kind: claim
status: holds
created: 2026-08-12
tags: d3d8
---

## Claim

Every texture the guest binds resolves to a GPU resource: the untextured draws in a gameplay run are untextured because the ENGINE bound nothing, not because this host lost the binding.

## Evidence

0 of 352,340 draws had a texture bound that could not be resolved (d3d8 device report, gameplay run). The three ways it could fail -- no object at the guest address, an object that is not a texture, a texture whose GPU resource was refused -- are each counted and the first of each is named, so a non-zero would say which.

## What would falsify it

a texture format this host refuses at CreateTexture would make the count non-zero without anything else changing; the count is printed with the draw total on every run, so watch that line rather than assuming this still holds
