---
id: 52
title: Every unlit surface was painted with the float bits of its own X coordinate
status: resolved
symptom: psychedelic rainbow colours on world geometry; smooth hue banding across terrain; models mottled; the UI and fonts look correct
tags: d3d8,gpu,shader,fvf,colour
created: 2026-08-11
updated: 2026-08-11
---

## Symptom

The main menu renders in the right SHAPE -- statue, columns, pyramids, menu
panels -- with impossible colours: the sky hot pink, the terrain magenta and
green, the statue a rainbow. The 2D UI, the fonts and the logo look correct.

## Cause

The fixed-function shader takes three vertex attributes and all three must be
bound, so when an FVF carries no `D3DFVF_DIFFUSE` the colour attribute was
pointed at the POSITION's bytes and, in the comment's words, "neutralised by
the uniforms instead". It was not neutralised: nothing told the shader, so
`v_color` was `UBYTE4_NORM` over the float bits of the vertex's X coordinate
and the fragment stage MODULATEd the texture by it. A float's byte pattern
varies smoothly with its value, which is why the result was a smooth rainbow
across a hillside rather than noise -- and why it read as a lighting bug.

The UI was unaffected because it uses `D3DFVF_XYZRHW | D3DFVF_DIFFUSE`: those
vertices really do carry a colour.

## Fix

`VertexUniforms.has_diffuse`, set from `color_offset >= 0`, and the vertex
shader uses `vec4(1.0)` when it is zero. White is D3D8's own answer for a
vertex with no diffuse and lighting disabled.

## Still approximate, and stated

With lighting ENABLED -- the engine sets 13 lights and a material -- D3D8
computes the vertex colour from the normals, the lights and the material.
This stage does not, so white leaves the texture as the artist authored it
rather than lit. That is a missing feature, named in the codemap, not a
mystery.

## Lesson

"Neutralised by the uniforms instead" was written as a statement of fact in a
comment and was never true. A binding that must exist but must not be READ
needs the shader to be told, and the comment claiming otherwise survived
because the only thing that would have contradicted it was a picture nobody
had looked at yet.
