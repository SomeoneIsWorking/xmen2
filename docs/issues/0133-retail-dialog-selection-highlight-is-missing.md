---
id: 133
title: Retail dialog selection highlight is missing
status: resolved
symptom: Selected options are not visibly highlighted in the New Game difficulty dialog or Quit Game Yes/No dialog
state_items: S004
tags: rendering,ui,dialog,highlight,d3d8,user-report
created: 2026-08-27
updated: 2026-08-28
---

## Root cause

The title builds the selected row's transform in `XMen2.exe FUN_005ea9e0` with
the linear scale

```
1.3359999656677246 - output_height * 0.000699999975040555
```

and calls transform builder `FUN_005707d0` from `0x005ead96` (return address
`0x005ead9b`). At 600 and 720 lines this yields `0.916` and `0.832`; at 2160
lines it yields `-0.176`. The selected row is still accepted and presented by
D3D8, but its scene-graph Y/Z scale has crossed zero and its visible height
collapses from the retail-relative ~3.3% of the output to ~0.65%.

This is a 2005 title-side UI-layout approximation extended beyond its intended
display range, not a missing texture, D3D8 rejection, font-scale defect, or
logical-backbuffer mismatch. The row is an untextured diffuse draw: eight
primitives, stride 16, FVF `0x42`.

## What was tried / dead ends

The original probe assumed the shipped `selected.png` dimensions (608x28), but
the D3D boundary never sees that source shape. Runtime observes three 128x32
DXT3 resources with the same committed-byte fingerprint
`a564975d5815f611`. A later six-vertex geometry signature did fire, but it
identified the NEW GAME selector still drawn behind the modal, so state
mutations against it could not answer the dialog-row question.

Instrument I072 v13 replaced the texture premise with an exact untextured draw
class and traced the submitted world matrix backward through
`igMatrix44f::multiply`, `igTransform::setMatrix`, title transform builder
`FUN_005707d0`, and its caller. The earlier texture/topology conclusions remain
dead ends; none is evidence about the selected row.

## Resolution

`src/native/dialog_selection_scale.c` retains the recompiled title transform
builder and changes Y/Z only for the exact selected-row return address, only
when both supplied values match the recovered retail formula. The pure policy
preserves that formula through the shared 800x600 retail UI reference and holds
the reference scale above it, preventing the obsolete linear approximation
from crossing zero without changing lower-resolution behavior.

`test_dialog_selection_scale_policy` pins the recovered formula and extension.
The cold-plus-warm `selector-dialog-800`, `selector-dialog-720`, and
`selector-dialog-4k` live cases each pass 15/15. Their selected-row heights are
respectively 20.04/600, 24.04/720, and 72.14/2160 pixels; the 800x600 runtime
reports zero corrections, the higher modes report one correction per reached
selected-row transform, and all three report zero input mismatches.
Claim C275 records the falsifier and code dependencies for this evidence.
