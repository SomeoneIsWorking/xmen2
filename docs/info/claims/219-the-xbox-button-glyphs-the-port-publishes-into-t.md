---
id: C219
kind: claim
status: holds
created: 2026-08-19
tags: glyphs,fonts
depends: tools/make_pad_font.py, src/native/pad_glyphs.c
---

## Claim

The Xbox button glyphs the port publishes into the PC medium font DO render in game. The delivery route is: FUN_006281f0 returns the glyph byte, FUN_00619e30 sprintf()s it into "[%s]" at 0x00a68c18, and the game's own text renderer draws it from the substituted x2f_med_pc atlas. Codepoints 0x80.. work; there is no printable-range restriction.

## Evidence

In-game capture, main menu prompt bar: '[B] BACK' with the red Xbox B button drawn, at shipping codepoint 0x80, with the shipping pack (scratch/shots/ship.png, zoomed scratch/shots/glyph_zoom.png). The label buffer read live as bytes 5b 80 5d. Before the fix the same setup drew '[] BACK' at every codepoint tried -- 0x01, 0x7b and 0x80 -- while a stock glyph ('Q', 0x51) drew normally.

## What would falsify it

if a prompt is ever seen with empty brackets while the pad is connected and the derived pack is active
