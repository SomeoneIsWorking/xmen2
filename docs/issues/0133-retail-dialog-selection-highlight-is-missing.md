---
id: 133
title: Retail dialog selection highlight is missing
status: investigating
symptom: Selected options are not visibly highlighted in the New Game difficulty dialog or Quit Game Yes/No dialog
state_items: S004
tags: rendering,ui,dialog,highlight,d3d8,user-report
created: 2026-08-27
updated: 2026-08-27
---

## Root cause

Not yet established. The missing 3840x2160 highlight is a real render-path
divergence, not an 800x600 logical-backbuffer artifact: after issue #135's cold
path repair, the fresh run creates a 3840x2160 D3D device and captures a real
3840x2160 frame, yet the horizontal orange bar behind NORMAL is absent. The
same bounded 800x600 route visibly renders that bar.

## What was tried / dead ends

The original probe assumed the shipped `selected.png` dimensions (608x28), but
the D3D boundary never sees that source shape. Runtime observes three 128x32
DXT3 resources with the same committed-byte fingerprint
`a564975d5815f611`. A later six-vertex geometry signature did fire, but it
identified the NEW GAME selector still drawn behind the modal, so state
mutations against it could not answer the dialog-row question.

Instrument I072 v5 now records immutable texture provenance, pre-build draw
geometry, and paired accepted/refused lowering results without claiming asset
identity. The repeatable 4K case produced 9,722 fully paired requests and at
least one runtime fingerprint. That proves the writer is live, but the known
128x32 candidates still do not identify the missing horizontal row; I072
therefore remains DISTRUSTED rather than converting reach into a root-cause
claim.

## Resolution

Pending. The next grounded step is to correlate dialog-only draw appearance
and disappearance across the 800x600 and 3840x2160 captures using v5's texture
fingerprint plus complete request state, then mutate only a candidate that the
instrument first proves belongs to the horizontal selected row.
