---
id: C275
kind: claim
status: holds
created: 2026-08-28
tags: rendering,ui,resolution
depends: src/native/dialog_selection_scale.c#x2_dialog_selection_transform, src/native/dialog_selection_scale_policy.c#x2_dialog_selection_scale, src/native/dialog_selection_scale_policy.c#x2_dialog_selection_offset_correction
---

## Claim

The retail dialog selected-row transform uses a title-side linear Y/Z scale that crosses zero at high output heights; the caller also derives the row's translation as 7.0 times that scale. The scoped extension corrects both, preserving the 800x600 result and restoring the retail-relative row height and placement at 720p and 4K.

## Evidence

test_dialog_selection_scale_policy plus selector-dialog-800, selector-dialog-720, and selector-dialog-4k live cases, each 15/15 on 2026-08-28; observed row heights 20.04, 24.04, and 72.14 pixels with zero formula mismatches. 2026-09-23 (#185): the same cases, now 16/16, also place the row top at 0.2996, 0.3013 and 0.3013 of output height; with only the scale corrected the 4K top was 0.3212, one row below its text, and the new check fails.

## What would falsify it

A captured selected-row draw at one of those resolutions has a mismatched title-builder caller/formula, the 800x600 output changes, the accepted row again falls below 2.5% of output height, or its top leaves 0.005 of the 800x600 share (0.2996)
