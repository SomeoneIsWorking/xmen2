---
id: 88
title: The four d-pad directions all draw the same glyph, so a four-way prompt is unreadable
status: resolved
symptom: a prompt offering four d-pad choices (e.g. select another character) draws the SAME d-pad icon four times -- nothing says which direction each choice needs
tags: pc,native,input,pad,glyphs,prompts,user-report
created: 2026-08-19
updated: 2026-08-20
---

REPORTED BY THE USER, 2026-08-19, playing with a pad. **The cause is confirmed
in this port's own source, not a theory.**

`src/native/pad_glyphs.c`:

    if (code >= 0x11u && code <= 0x14u) return X2_PAD_GLYPH_DPAD;

All four POV directions map to ONE codepoint, and `assets/buttons/btn_dpad.svg`
is a plain four-armed cross with no arm highlighted. So the game asks to name
four different bindings, this port answers with the same picture four times,
and the prompt is strictly LESS informative than the keyboard text it replaced
-- `[UP] [DOWN] [LEFT] [RIGHT]` at least said which.

The directions are not ambiguous in the data; only in the art. From
`src/native/xbox_defaults.c` the port already binds them individually:

    0x11 POV X+  d-pad right   IncreaseHeroAggr
    0x12 POV X-  d-pad left    DecreaseHeroAggr
    0x13 POV Y+  d-pad down    PreviousHero
    0x14 POV Y-  d-pad up      NextHero

## The fix

Four directional glyphs -- the same cross with the relevant arm filled -- and
`pad_glyph_code` returning one per code instead of collapsing the range. That
means three more published codepoints (11 icons becomes 14); the band in
x2f_med_pc is 68 rows and an icon costs 20, so three rows of 12 fit and the
space is there.

`assets/buttons/glyphs.json` is the ordered codepoint authority and the
generated `pad_glyph_codes.h` follows it, so the change is: new SVGs, extend
that list, map each code. The builder's own verification (every published pixel
read back, every changed pixel inside a cell) covers the new ones for free.

### Resolution (2026-08-20)
Resolved: the shared gamepad-xbox360 set supplies four direction-highlighted SVGs; the manifest publishes all four, pad_glyph_code maps 0x11..0x14 independently, the shipping-wrapper test rejects glyph collisions, and an 18x18 raster audit at scratch/screenshots/glyph-audit/sheet-rgb.png keeps every highlighted direction legible.
