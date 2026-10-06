---
id: C164
kind: claim
status: holds
created: 2026-08-12
tags: assets,glyphs,xbox
---

## Claim

The Xbox HUD font's glyph ART is identical to the PC build's -- the 82% on-disk difference is encoding, not different glyphs

## Evidence

Re-measured with the extractor FIXED (it used to collapse an IGB's images into one PNG per mip SIZE and emit only the last -- instrument I045). Both the PC install's x2f_hud_pc.igb and the Xbox x2f_hud_xbox.igb decode to 9 igImages, one per mip from 256x256 down to 1x1, and the 256x256, 128x128 and 64x64 levels are byte-identical (0 of 262,400 / 65,664 / 16,448). The FILES differ in 76,452 of 93,288 bytes, so that difference is in how the pixels are stored and in metadata, not in the glyphs. The Xbox assetsfb.wad's 2,520 entries hold no button, joy, glyph, pad or prompt asset.

## What would falsify it

An igImage the extractor cannot decode (it now WARNS and names the count when that happens, and warned for neither of these files), or Xbox button-glyph art found in the part of the ISO outside assetsfb.wad or inside default.xbe -- either would mean the search was too narrow rather than the art absent.
