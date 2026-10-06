---
id: C163
kind: claim
status: falsified
created: 2026-08-12
tags: assets,glyphs,xbox
falsified_on: 2026-08-12
---

## Claim

The 'Xbox' HUD font is byte-identical to the PC one -- there are no Xbox button glyphs in the font assets

## Evidence

textures/fonts/x2f_hud_xbox.igb and x2f_hud.igb decode to byte-identical glyph atlases at every mip level (0 bytes differ of 262,400 / 65,664 / 16,448 at 256x256 / 128x128 / 64x64), and ui/fonts/x2f_hud_xbox.xmlb, x2f_hud.xmlb and x2f_hud_gc.xmlb are the same 27,082 bytes with the same md5. The Xbox assetsfb.wad's 2,520 entries contain no button, joy, glyph, pad or prompt asset.

## What would falsify it

Finding Xbox button-glyph art anywhere in the Xbox build -- in the part of the ISO outside assetsfb.wad, or inside default.xbe -- which would mean the search was too narrow rather than the premise wrong. Note the atlas comparison used the decoded PNGs, so a difference the IGB decoder drops (a second texture page, a palette) would also be invisible to it.

## FALSIFIED 2026-08-12

FALSIFIED by its own falsifier. The comparison used the DECODED atlases, and tools/extract_font_igb.py writes one PNG per mip SIZE -- so an IGB holding several igImages has them overwrite each other under the same filenames, and what was compared was whichever happened to be written last. Comparing the FILES instead: the PC install's x2f_hud_pc.igb and the Xbox x2f_hud_xbox.igb are the same size (93,288 bytes) and differ in 76,452 of them (82%), across 30 runs spanning 0xe88 to the end of the file. The two fonts are not the same asset. Also wrong in the earlier comparison: x2f_hud.igb and x2f_hud_xbox.igb were BOTH taken from the Xbox ISO, so that pair could not have answered the question -- the PC build ships x2f_hud_pc.igb and it was never in the comparison.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
