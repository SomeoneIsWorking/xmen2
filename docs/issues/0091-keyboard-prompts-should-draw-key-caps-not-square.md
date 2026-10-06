---
id: 91
title: Keyboard prompts should draw key caps, not square brackets
status: resolved
tags: pc,native,input,keyboard,glyphs,prompts,assets,user-report
created: 2026-08-19
updated: 2026-08-21
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

The first proposed sequence, `<left><letters><right>`, was incomplete: a
one-pass font cannot draw a background behind letters that were already drawn.
The working sequence emits left and overlapping middle strips first, repeats an
invisible negative-advance glyph to rewind the pen, then emits the retail name
and right edge. That ordering is what lets the stock letters draw over the cap
while the final text width still follows the actual binding name.

### Resolution (2026-08-21)
Resolved by a four-codepoint composition over the shared blank keycap: left and overlapping middle pieces draw first, an invisible negative-advance glyph rewinds the pen, the retail binding name draws on top, and a right edge closes at the name's real width. This keeps arbitrary rebindings dynamic. The real x2f_med_PC pack published 19 drawing glyphs with 5,580 changed pixels confined to their cells; 51-test suite coverage includes ENTER, A, pad and ??? labels. A no-pad, --no-window tutorial run captured ENTER centered on the cap with correct CONTINUE spacing at scratch/screenshots/keycap-windowless.png.
