---
id: C163
kind: claim
status: holds
created: 2026-08-12
tags: assets,glyphs,xbox
---

## Claim

The 'Xbox' HUD font is byte-identical to the PC one -- there are no Xbox button glyphs in the font assets

## Evidence

textures/fonts/x2f_hud_xbox.igb and x2f_hud.igb decode to byte-identical glyph atlases at every mip level (0 bytes differ of 262,400 / 65,664 / 16,448 at 256x256 / 128x128 / 64x64), and ui/fonts/x2f_hud_xbox.xmlb, x2f_hud.xmlb and x2f_hud_gc.xmlb are the same 27,082 bytes with the same md5. The Xbox assetsfb.wad's 2,520 entries contain no button, joy, glyph, pad or prompt asset.

## What would falsify it

Finding Xbox button-glyph art anywhere in the Xbox build -- in the part of the ISO outside assetsfb.wad, or inside default.xbe -- which would mean the search was too narrow rather than the premise wrong. Note the atlas comparison used the decoded PNGs, so a difference the IGB decoder drops (a second texture page, a palette) would also be invisible to it.
