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


## Narrowed: it is ONE draw, and it runs off the end of its index buffer

Not a glyph-atlas or format problem. The Vulkan validation layer flagged it and
`src/gpu/gpu_draw.c` now refuses it by name, once per frame:

    gpu: draw REFUSED -- the index range runs off the end of the bound index buffer.
      204 index/indices of 2 byte(s) from index 0 needs 408 byte(s); the buffer is 152.
      primitive 2, 202 primitive(s), base vertex 0.

A 202-primitive triangle STRIP needs 204 indices; the bound buffer holds 76.
Everything else in the frame draws correctly -- including OTHER text: the
title screen's legal paragraph renders readably in the headless capture. So
whatever is wrong is specific to this one draw's index buffer, not to text as
such.

Two candidates, and the numbers distinguish them:

1. **The engine bound a buffer it did not fill** -- a different (larger) index
   buffer was meant. Log every `CreateIndexBuffer` length and every
   `SetIndices` object and correlate with the failing draw.
2. **This layer sized the buffer wrong on creation.** `CreateIndexBuffer` takes
   a length in BYTES and passes it straight through, so this would have to be
   the guest passing something this layer misreads -- the ABI table's argument
   count for slot 24 is the thing to check.

The refusal is deliberate: a clamped draw would render a shorter version of
whatever the engine asked for, which is a subtly wrong picture that leads
nowhere. Refused, it is a missing caption that leads straight to this line.
