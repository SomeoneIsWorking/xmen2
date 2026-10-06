---
id: C220
kind: claim
status: holds
created: 2026-08-19
tags: glyphs,fonts,re
depends: tools/make_pad_font.py
---

## Claim

A font glyph published with metrics in NORMALISED units instead of pixels still draws -- at a fraction of a pixel. XMen2.exe's font metrics (width, height, horizadvance, baseline) are pixels: 'A' is width=14 height=13 and its UV rect is exactly 14x13 of the 256x256 atlas.

## Evidence

tools/make_pad_font.py divided the 18px cell by the font's line height (20), publishing height=0.9 and horizadvance=0.95 against the font's own 3..22. The game drew those glyphs faithfully and invisibly, which was indistinguishable from 'the renderer refuses our codepoint' and sent the investigation through the codepoint, the font choice and the pixel format first. Writing pixels made the same glyph appear immediately. make_pad_font.py now REFUSES to publish a glyph whose height falls outside the range the font's own glyphs use.

## What would falsify it

if a shipped glyph is found whose width/height are not the pixel size of its own UV rect
