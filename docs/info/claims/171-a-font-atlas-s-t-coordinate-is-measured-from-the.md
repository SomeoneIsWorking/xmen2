---
id: C171
kind: claim
status: holds
created: 2026-08-13
tags: fonts,igb,ui
---

## Claim

A font atlas's t coordinate is measured from the BOTTOM of the image: decoded row = height - t*height

## Evidence

Counting inked pixels that fall inside the 166 glyph rects of x2f_med_pc, read both ways: t as-is captures 8315, t from the bottom captures 17944 -- and 17944 is the TOTAL number of inked pixels in the atlas, so the flipped reading accounts for every one of them and the as-is reading for 46%. The atlas also has a 68-row band with no alpha at decoded rows 0..67, which under the flipped reading is t 0.734..1.0, exactly where the glyphs stop (max t2 = 0.7344). tools/make_button_font.py writes t = y0/atlas_height from a TOP-DOWN measurement, so any glyph it publishes is placed mirrored in t.

## What would falsify it

if a glyph published with t measured from the bottom draws mirrored or blank in the game, or if another shipped atlas puts ink inside its rects only under the as-is reading
