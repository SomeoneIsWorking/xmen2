---
id: 38
title: Text glyphs are wrong in the native render -- the panel draws correctly, the letters do not
status: open
symptom: scratch/screenshots/native.png: the save/load panel, its border, tabs and cursor are all correct, but the caption renders as broken glyph fragments instead of readable text
tags: pc,native,graphics,d3d8,text
created: 2026-08-07
updated: 2026-08-07
---

## What is right, and what is not

The first native capture (C142) shows the game's save/load panel drawn
correctly: gradient fill, bevelled border, the two tabs along the bottom, the
cursor. Only the TEXT is wrong -- the caption is a row of broken glyph
fragments rather than letters.

So this is not "the renderer does not work". Geometry, transforms, blending and
texture sampling all produce a correct panel. Whatever is wrong is specific to
how glyphs are drawn.

## Candidates, in the order they are cheap to test

1. **The glyph atlas sub-rect.** Text is a run of quads sampling small
   rectangles of one texture. A UV that is off by a texel, or that ignores a
   non-zero base level, breaks letters while leaving big untextured quads
   perfect -- which is exactly the split seen here.
2. **A texture format the panel does not use.** Font atlases are commonly A8 or
   A4R4G4B4; the panel art is not. `d3d8: ... format` in the run log says which
   formats were created, and `src/gpu` says which it maps.
3. **The alpha-test / texture-stage combination for text.** The panel is opaque
   fill; glyphs are alpha-tested cutouts. `d3d8_drawcall_note_ignored_state`
   already records state the draw path ignores -- read that list first.

## How to look

`tools/native_shot.sh` gives a picture per run. The state the text draws under
is what `d3d8_state_report` prints at shutdown; the ignored-state list is the
first thing to read, because a state the draw path drops is invisible in every
other way.
