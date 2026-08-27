---
id: C275
kind: claim
status: holds
created: 2026-08-28
tags: rendering,ui,resolution
depends: src/native/dialog_selection_scale.c#x2_dialog_selection_transform, src/native/dialog_selection_scale_policy.c#x2_dialog_selection_scale
---

## Claim

The retail dialog selected-row transform uses a title-side linear Y/Z scale that crosses zero at high output heights; the scoped extension preserves the 800x600 result and restores retail-relative row height at 720p and 4K.

## Evidence

test_dialog_selection_scale_policy plus selector-dialog-800, selector-dialog-720, and selector-dialog-4k live cases, each 15/15 on 2026-08-28; observed row heights 20.04, 24.04, and 72.14 pixels with zero formula mismatches

## What would falsify it

A captured selected-row draw at one of those resolutions has a mismatched title-builder caller/formula, the 800x600 output changes, or the accepted row again falls below 2.5% of output height
