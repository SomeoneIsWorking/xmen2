---
id: 89
title: RB and RT draw the same letter R, so a prompt does not say which to press
status: open
symptom: a prompt shows a shoulder button reading R and there is no way to tell whether it means the bumper or the trigger
tags: pc,native,input,pad,glyphs,prompts,user-report
created: 2026-08-19
updated: 2026-08-19
---

REPORTED BY THE USER, 2026-08-19, playing with a pad: "the button says R but is
it RB or RT? should be clear".

**Confirmed by reading this port's own art.** `assets/buttons/btn_rb.svg` and
`btn_rt.svg` are the SAME rounded rectangle with the SAME single letter `R`,
differing only in fill: RB is light with black text, RT is dark with white
text. `btn_lb.svg` / `btn_lt.svg` are the same pair with `L`.

Inverted colour is not a distinguishing cue at the size these draw. The cell is
18x18 pixels in a font atlas, drawn into a HUD over arbitrary background art,
and the two glyphs have identical silhouettes -- so the only signal separating
"press the bumper" from "press the trigger" is which of two greys the inside
happens to be. The player cannot act on that, which is the whole point of a
button prompt.

It also matters more here than it would elsewhere, because the port's preset
binds `Power` to RT (`Z-`, code 0x06) while LB and RB are bound to nothing at
all -- so a prompt that reads as a bumper names an input that does nothing.

## The fix, and how to know it worked

Give the bumper and the trigger DIFFERENT SILHOUETTES rather than different
fills, and carry the letters as well: a bumper is a flat bar, a trigger is a
paddle. Both cues, so neither has to survive alone.

This must be checked by RASTERISING at the shipped 18x18 and looking, not by
looking at the SVG -- a design that separates at 72x72 and collapses at 18x18
is exactly what is already wrong here. The builder already rasterises through
ImageMagick, so the check is a zoomed PNG of the four glyphs side by side
before anything is published.
