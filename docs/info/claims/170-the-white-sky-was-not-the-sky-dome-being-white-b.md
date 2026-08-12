---
id: C170
kind: claim
status: holds
created: 2026-08-12
tags: renderer
---

## Claim

The white sky was NOT the sky dome being white by D3D8 rules -- it was the backend bypassing the texture combiner whenever no texture was bound. D3D8 computes COLOROP over its arguments regardless of what is bound, so D3DTOP_SELECTARG2 with D3DTA_TFACTOR yields the texture-factor colour and samples nothing. Fixing that (plus implementing SELECTARG2, which had been silently treated as MODULATE) turns the menu sky from flat white into its gradient.

## Evidence

Measured before the fix: 51,310 untextured draws in a menu run, 1,070 of them COLOROP 3 (SELECTARG2) ARG2 TFACTOR. After the fix the untextured histogram holds only COLOROP 1 (DISABLE) and the menu capture's sky corner reads (93,141,204) instead of (255,255,255), with 0 draws refused and all 10 d3d8 self-tests passing.

## What would falsify it

if a capture shows the sky the wrong colour rather than merely coloured -- the texture factor is being read but not the rest of the stage -- or if some other draw is what changed
