---
id: 196
title: Mikhail Rangers render as black silhouettes
status: open
symptom: a Mikhail Ranger draws solid black while the party beside it is lit and textured; the user reports two more enemy types doing the same
state_items:
tags: pc,native,gpu,d3d8,rendering
created: 2026-10-20
updated: 2026-10-20
---

# 0196 — Mikhail Rangers render as black silhouettes

## Reproduction

`tools/live_case.py ranger-render`: Continue the autosave, `loadmap
act1/deadzone/deadzone2`, spawn `_ms_ss_mikhailranger_a` 120 units from the
active hero through `/script`. The shot `enemy.png` shows the Ranger as a
black silhouette beside a normally lit party; the renderer refuses nothing.

Booting with `X2_BOOT_MAP=act1/deadzone/deadzone2` draws the whole party
black the same way, while the Continue route draws them normally.

## Cause

Not yet traced. The Ranger uses `78_mikhail_ranger` skins 7801-7805.
