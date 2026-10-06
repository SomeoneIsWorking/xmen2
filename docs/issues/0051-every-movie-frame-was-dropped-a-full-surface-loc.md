---
id: 51
title: Every movie frame was dropped: a full-surface LockRect was refused as a sub-rectangle
status: resolved
symptom: movie plays but the picture never changes; d3d8: LockRect asked for the sub-rectangle (0,0)-(640,480); this host locks whole surfaces only
tags: d3d8,movie,lockrect,surface
created: 2026-08-11
updated: 2026-08-11
---

## Symptom

Running `x2native --run --d3d8`, the intro movies present at ~30 fps but the
d3d8 log carries one line per frame:

    d3d8: LockRect asked for the sub-rectangle (0,0)-(640,480); this host locks whole surfaces only.

and, roughly 30 seconds in, the run aborted out of
`WaitForSingleObject(INFINITE)` with the movie's decoder thread suspended.

## Cause

`surf_LockRect` refused ANY non-NULL `pRect`. The movie player passes one
every frame -- and the rectangle it passes is (0,0)-(640,480) of its 640x480
surface, i.e. the whole thing. The refusal returned `D3DERR_INVALIDCALL`, so
the decoder never got a pointer, never wrote a frame, and the handshake it
drives stalled.

The refusal was written as caution ("a sub-rectangle lock needs the offset
arithmetic and the engine has not asked for one"). The engine HAD asked for
one, from the first movie onward; the comment was a guess that was never
re-checked against a run.

## Fix

The offset arithmetic, in `src/d3d8/d3d8_surface.c` and the same shape in
`d3d8_resource.c`'s texture LockRect: the pointer is the first pixel inside
the rectangle, the pitch is unchanged. Block-compressed formats require a
4x4-aligned origin and recover the block size from the pitch. A rectangle
that is empty or outside the surface is still refused, by name.

Covered by the cube self-test (`--d3d8-selftest`), which locks (8,4)-(16,12)
of a 32x32 BGRA8 face and checks the pointer is `base + 4*pitch + 8*4` -- a
zero offset, i.e. the old behaviour dressed up as success, fails it.

## Lesson

A refusal is only honest while its premise still holds. This one said "the
engine has not asked for one" and kept saying it long after the engine did,
because nothing re-tested the premise. The message was printed per frame and
still read as background noise rather than as the reason the picture was
blank.
