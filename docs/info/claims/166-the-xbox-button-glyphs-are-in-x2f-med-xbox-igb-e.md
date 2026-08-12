---
id: C166
kind: claim
status: holds
created: 2026-08-12
tags: glyphs,xbox,assets
---

## Claim

The Xbox button glyphs ARE in x2f_med_xbox.igb -- extra cells the PC medium font does not have

## Evidence

Decoded both 256x256 atlases with the fixed extractor and compared them as IMAGES, both directions. The Xbox medium font carries a block of button art the PC one lacks: a d-pad cross, dark shoulder/trigger shapes, a row of gold face buttons and a row of coloured button squares. The PC atlas has the same Latin letters and stops short of those rows. Byte-wise the two differ in 174,548 of 262,400 (66.5%), and the font-set files name exactly this pair as the platform difference (C165).

## What would falsify it

Substituting x2f_med_xbox.igb for X2F_med_PC and seeing a prompt that draws one of those cells render unchanged -- which would mean the glyph is selected by a codepoint the PC strings never emit, and the strings need substituting too. Note the PC prompt reads '[ENTER] CONTINUE' as literal letters, so that is the likely case.
