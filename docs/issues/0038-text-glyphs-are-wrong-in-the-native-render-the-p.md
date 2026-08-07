---
id: 38
title: Text glyphs are wrong in the native render -- the panel draws correctly, the letters do not
status: resolved
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


## Root cause: a NON-indexed draw was taking the indexed path

Not the glyph atlas, not the texture format, not the alpha test -- and not text
at all. `SetIndices` is STATE: it stays bound across draws, and `DrawPrimitive`
(which takes no indices) must ignore it. `fill_request` carried the bound index
buffer into EVERY draw, so the backend saw a non-NULL index buffer and took its
indexed path for a non-indexed draw: a 202-primitive strip -- 204 VERTICES --
was drawn as 204 INDICES out of whatever index buffer happened to be bound,
which held 76.

One line: `fill_request` now takes an `indexed` flag, and `DrawPrimitive`
passes 0.

After it, `refused 0` and the caption reads **"SAVE FAILED!"** with
**"[Esc] CANCEL   [Enter] RETRY"** along the bottom -- see
`scratch/screenshots/native.png`.

## How it was found, and the two wrong turns

The Vulkan validation layer flagged the out-of-range draw; `gpu_draw` now
refuses it by name with the numbers, which is what made it visible without a
validation layer at 60fps.

**Wrong turn 1: state blocks.** The engine creates, applies and deletes exactly
one state block per frame, and exactly one draw per frame was failing. That
correlation is as strong as a correlation gets and it was a coincidence:
printing the index binding on both sides of every `ApplyStateBlock` showed it
`unchanged` every time.

**Wrong turn 2: a stale GPU handle.** The D3D8 layer's record of the buffer's
size and the GPU layer's disagreed (408+ vs 152), which pointed at a recycled
handle. That reading was wrong too, but it exposed a REAL defect on the way:
the device did not hold a reference on what was bound to it, though D3D8 does.
The engine creates an index buffer per mesh, binds it, draws and releases it,
expecting the device's own reference to keep it alive -- so without one the
object is retired and its GPU buffer destroyed while still bound.
`SetIndices`/`SetStreamSource`/`SetTexture` now addref the new binding and
release the old, and `ApplyStateBlock` re-establishes the same invariant after
it replaces the bindings wholesale. That fix is kept: it was not the cause
here, and it is a use-after-free waiting for a different frame.

The thing that actually identified it was the two layers' probes disagreeing
about the same draw -- the D3D8-level check never fired while the GPU-level one
did, every frame. Two instruments contradicting each other is not a tie; it
means a third thing is true. Here it was that the failing draw never went
through `DrawIndexedPrimitive` at all.
