---
id: C221
kind: claim
status: holds
created: 2026-08-19
tags: glyphs,fonts
depends: tools/make_pad_font.py
---

## Claim

tools/make_pad_font.py's decode_atlas returns a BOTTOM-UP buffer while the font's glyph UVs are top-down into the real texture. Art blitted into it must be mirrored vertically or it draws upside down, and row_to_t already accounts for the row order so the cell selection is right either way.

## Evidence

Measured on a stock glyph rather than reasoned: reading 'A' (t=0.3594..0.4102) out of the decoded atlas bottom-up gives an upside-down A, top-down gives an unrelated stroke. In game, before the fix, a forced LB glyph drew as an upside-down L while the atlas cell held an upright one; after mirroring at blit it draws upright (scratch/shots/lb_side3.png). B, A, X and Y are all near-symmetric, which is why this survived an in-game capture -- the discriminator had to be an ASYMMETRIC glyph, forced onto every prompt with X2_PAD_GLYPH_PROBE=0x84.

## What would falsify it

if a stock glyph is ever found that reads upright from the decoded buffer top-down
