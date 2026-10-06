---
id: I056
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

X2_FRAME_TABLE (src/d3d8/d3d8_drawcall.c)

## Validated by

Lists every draw of one gameplay frame with the screen rectangle its own vertices project to, its object-space extents, normal lengths, and a CPU replication of the vertex shader's lighting broken into its factors. It produced BOTH answers in one frame -- LIT 1.000 for emissive props and 0.077 for the black character -- which is what makes the dark reading meaningful. FOUR defects of its own were found and fixed while using it: it printed 'tex 0' for all 434 draws of a plainly textured frame (it ran before the texture was resolved); it printed '|N| 0.000' identically for 'this FVF has no normal' and 'these normals measure zero', which briefly looked like 31 dead-normal draws; a vertex on the near plane made a rectangle read x -819065..41523; and an 80-byte buffer silently truncated the factor breakdown mid-word.

## Known failure modes

(none recorded yet)
