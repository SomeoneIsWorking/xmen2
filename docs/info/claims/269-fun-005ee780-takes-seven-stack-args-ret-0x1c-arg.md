---
id: C269
kind: claim
status: holds
created: 2026-08-26
tags: text,glyphs,prompts,renderer
depends: src/native/prompt_glyph_draw.c#x2_override_005ee400, src/native/prompt_glyph_metrics.c#x2_prompt_glyph_publish_metrics
---

## Claim

FUN_005ee780 takes seven stack args (RET 0x1c): arg1 = the wide string (entry_esp+4), arg2 = the batch object, arg3 = pen x start, arg4 = pen y, arg5 = the draw/style object, arg6 = pen base (pen = *(arg6) + arg3), arg7 = the object holding the atlas dims (512x256) and UV scales (1/512, 1/256) cached by FUN_005ee620. The pen is a LOCAL advanced per character and never written back. FUN_005ee400 is (x0,y0,x1,y1,u0,v0,u1,v1) with ECX = arg2; it applies no arithmetic, pushing four vertices through [ECX]->vtable+0xc. With an untouched font record, the port's codepoints 0x90..0x93 emit degenerate zero-size quads with zero advance; the native path now publishes in-memory metrics and intercepts those emitted rectangles without modifying font pixels or UVs.

## Evidence

scratch/logs/quad-err.log and scratch/logs/glyphargs-err.log, from the retired scoped argument probes. Drawing 0090 0091x5 0092x5 'Enter' 0093 emitted 11 degenerate quads at pen x 19.022 with UVs (-0.001,0.998,-0.001,0.998), then 5 real quads for 'Enter' advancing 18.489/25.067/33.600/39.111/45.689, then a final degenerate quad at 53.333. Arguments were dumped from the live call rather than derived from frame arithmetic, which is what produced the earlier EDX error (I069). The probes were removed after the shipping interception and metric owners encoded the established ABI.

## What would falsify it

a live X2_GLYPH_ARGS dump in which arg1 is not the wide string being walked, the pen start is not *(arg6)+arg3, or FUN_005ee400's eight floats are not the quad corners followed by the UVs; or a prompt codepoint that emits a non-degenerate quad with the font untouched
