---
id: C165
kind: claim
status: holds
created: 2026-08-12
tags: assets,glyphs,xbox
---

## Claim

The Xbox/PC font difference is X2F_med and X2F_thin, NOT the HUD font -- both platforms use X2F_hud_PS2

## Evidence

ui/fonts/fonts_pc.xmlb and fonts_xbox.xmlb (392 and 396 bytes, extracted from the Xbox assetsfb.wad) have string tables differing in exactly two entries: X2F_med_PC/X2F_thin_PC against X2F_med_XBOX/X2F_thin_XBOX. Both name X2F_hud_PS2 for the hud slot, which is why the PC install's x2f_hud_pc.igb has the same md5 as the ISO's x2f_hud_ps2.igb. The art: x2f_med_pc.igb vs x2f_med_xbox.igb differ in 174,548 of 262,400 bytes (66.5%) at 256x256, and decode to 1 and 9 igImages respectively.

## What would falsify it

The game loading a hud font other than X2F_hud_PS2 (the file instrument lists every open by name, so this is checkable in one run), or X2F_med_XBOX substituted for X2F_med_PC producing no visible change in menu text -- which would mean the font set is not what selects the drawn art.
