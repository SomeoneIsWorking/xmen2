---
id: C244
kind: claim
status: holds
created: 2026-08-22
tags: shadow,renderer,retail
depends: docs/RE/shadows.md
---

## Claim

Retail DetailedShadow is disconnected from shadow rendering: its backing byte has exactly four default/load/save/settings-reload references, while CShadowMgr independently produces procedural six-vertex floor-decal fans that the native renderer submits.

## Evidence

Static XMen2 xrefs at 0x00a68d40; CShadowMgr call graph at 0x004b64e0/0x004b5700; matched stock fan4 draws and existing native frame dumps, documented with denominators in docs/RE/shadows.md.

## What would falsify it

A changed retail executable adds a gameplay/render reference to 0x00a68d40, or a matched capture shows the CShadowMgr floor-fan route diverges before native GPU submission.
