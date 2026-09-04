---
id: C004
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

A hand-built PE proxy libIGDisplay.dll sits transparently in the real game's load path: XMen2.exe runs to the same splash sequence with it as without, with zero new errors and exactly one extra module loaded.

## Evidence

tools/run_shim.py stock vs proxy, 60s each, Wine + DXVK/lavapipe on Xvfb. Game-process module sets: 53 vs 54, sole delta libIGDisplay_orig.dll. Unique-error diff between the two logs is empty. Both frames show the boot movie sequence (Beenox / Vicarious Visions Alchemy splash).

## What would falsify it

Replacing any of the 898 forwarders with real code and seeing a divergence would show the transparency claim only ever covered pass-through, not reimplementation. Also: this proves load-time and early-boot transparency ONLY -- nothing past the splash has been exercised.
