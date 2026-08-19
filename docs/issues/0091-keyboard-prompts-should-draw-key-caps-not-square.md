---
id: 91
title: Keyboard prompts should draw key caps, not square brackets
status: open
tags: pc,native,input,keyboard,glyphs,prompts,assets,user-report
created: 2026-08-19
updated: 2026-08-19
---

REQUESTED by the user, 2026-08-19: the keyboard side of a prompt should use the
key-cap art rather than the game's `[ENTER]` text convention. The art exists --
`shared/port-assets`, set `keyboard`, taken from zelda3d, which draws its HUD
badges this way.

## The one design decision already made, and it is the right one

**The label is not baked into the cap.** zelda3d composites the key that is
actually bound over one blank cap, so a rebind changes the prompt with no new
asset. That is what makes a keyboard set finite: a hundred key names, one cap.
`tools/draw_keyboard.py --label ENTER` in that repo composes one.

## What makes this harder here than the pad glyphs were

The pad case fits the game's own text pipeline because a button is ONE glyph:
the naming override returns a single byte and the font draws it. A key name is
not one glyph -- `ENTER` is five characters wide, and this port publishes into
an 18x18 cell of a 256x256 atlas with 68 free rows.

So the options are, in rough order of how much they cost:

1. **Cap art around the existing letters**: publish a left cap edge, a middle
   and a right cap edge as three glyphs, and have the label override emit
   `<left><E><N><T><E><R><right>`. The game's own text layout does the rest,
   the letters stay the font's, and it costs three codepoints. The seams have
   to line up at whatever size the font draws, which is the thing to check
   first with a rasterised sheet.
2. **A cap per key that a prompt actually names**, rendered at pack-build time
   into spare codepoints. Bounded by the free band (roughly 36 cells at
   18x18, 14 used), and it only works if the set of prompted keys is small.
3. Leave the brackets on the keyboard side. They do read as a key, which is why
   the bracket-stripping override only strips them around a GLYPH.

Start with (1): it is the only one that scales to an arbitrary rebind, which is
the same property that made the zelda3d design right.
