---
id: C152
kind: claim
status: holds
created: 2026-08-12
tags: native
---

## Claim

The PC native --d3d8 run reaches GAMEPLAY: it loads and renders a level, simulates it, and reaches the in-game death dialog drawn over the live level.

## Evidence

scratch/screenshots/level5.png from scratch/logs/level5.log -- X2_INPUT_SCRIPT skips the cutscene with Escape; the run ends on its own 340s timeout (exit 124), 6214 scenes / 562153 draws / 6213 presents, ~33 fps in the last 45s window, with no abort, no unsupported instruction and no unknown call. The dialog reads 'All X-Men have been eliminated / Load Game / Main Menu' over the level geometry.

## What would falsify it

a run that reaches the same dialog WITHOUT loading a level would mean the dialog is a menu artefact rather than proof of gameplay; check that the shot shows level geometry behind the panel and that scenes/draws keep climbing before it appears
