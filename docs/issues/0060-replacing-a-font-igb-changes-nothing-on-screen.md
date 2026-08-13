---
id: 60
title: A font substitution that changed nothing, twice -- and the second 'discriminator' was the bug, not the mechanism
status: resolved
symptom: "assets: REPLACED textures/fonts/X2F_hud_PC.igb ... and the rendered caption is byte-identical to the control"
tags: pc,native,assets,fonts,graphics,glyphs,dead-end
updated: 2026-08-13
---

## What was being attempted

Feature 3 (Xbox button prompts). With `X2_ASSETS` redirecting file opens, the
Xbox HUD font from the Xbox build was dropped in over the PC one:

    scratch/xboxglyphs/textures/fonts/x2f_hud_pc.igb   <- x2f_hud_xbox.igb
    scratch/xboxglyphs/ui/fonts/x2f_hud_pc.xmlb        <- x2f_hud_xbox.xmlb

The run reported both files replaced, submitted 58,343 draws with **0 refused**,
and rendered a cutscene caption that looked correct. That reads exactly like
success.

## It is not success, and the discriminator is what said so

The same substitution was repeated with a font that CANNOT look the same --
`font_XMEN_digital.igb`, a completely different typeface -- put in place of the
HUD font. The caption rendered **identically**. Then all three fonts the run
loads (`x2f_hud_pc`, `x2f_med_pc`, `x2f_big`) were replaced with that same
wrong font at once. The frame was **still identical**.

So replacing `textures/fonts/*.igb` does not change what is drawn. The
"correct-looking" Xbox run proved nothing: an unchanged picture is what this
path produces whatever you put in it.

**This is the discriminator rule earning its place.** The positive case alone
(Xbox font in, sensible text out, no draws refused) was about to be recorded as
the feature working.

## What is actually established

* `X2_ASSETS` redirects both `CreateFileA` and the CRT's `fopen`, and the
  redirect fires -- the report names each replaced file.
* The game really does open `textures/fonts/X2F_hud_PC.igb` and
  `ui/fonts/X2F_hud_PC.xmlb`, through the CRT.
* An Xbox IGB in that position causes no refused draw and no complaint.
* And none of that reaches the glyphs on screen.

## What is NOT established, and the next question

Where the drawn font texture actually comes from. Candidates, in the order
worth testing:

1. **A package.** The install has packaged asset sets; if the font texture is
   read out of one of those, the loose `.igb` open is a probe or a fallback that
   loses, and the replacement has to happen at the package level.
2. **A preload.** The font may be loaded once, early, from somewhere the
   instrument does not yet cover -- `CreateFileMappingA`/`MapViewOfFile` is a
   real `mmap` in this host and is NOT yet routed through the shared resolver
   (`k32_open_path`), which is a gap this issue names.
3. **A silent parse failure with a fallback.** The engine may read the IGB, fail
   to accept it, and keep whatever it had. Nothing currently reports that.

The instrument to build first is (2): route the mapping path through the same
resolver as the other two opens, so the "what did this run open" list stops
having a hole in exactly the place the missing asset would be.

## Note on the earlier claim

An earlier version of the file instrument watched only `CreateFileA` and
reported **31** names for a run that had loaded fonts, models and packages.
Routing the CRT's `fopen` through the same resolver took that to **352**. The
conclusion drawn from the 31-name list -- "`joy1..4.png` are never opened, so
the button prompts cannot be reached" -- was drawn from a blind instrument and
must not be relied on.


## RESOLVED, and the resolution reverses the conclusion above

**Font replacement works. It reaches the pixels.** Everything above about the
loose `.igb` files not being where the glyphs come from is WRONG, and the way
it was wrong is worth more than the issue was.

The "discriminator" was `font_XMEN_digital.igb` substituted over the other
fonts, tested against a cutscene caption. That caption is drawn in a squarish
green LED-like face -- **which is `font_XMEN_digital` itself**. So the test
replaced other fonts with the one the caption already used, and then concluded
from an unchanged caption that replacement does nothing. It was a no-op dressed
up as a control.

Re-run with `x2f_big.igb` over `x2f_hud_pc`, `x2f_med_pc` and
`font_xmen_digital`, the caption's second line changed from **"GREENLAND" to
"G"** -- the substituted font drawn through metrics that do not match it. The
first line is unchanged because its font was the one used as the SOURCE.

So:

* `X2_ASSETS` replacement reaches the renderer, for fonts, verified.
* The Xbox HUD font substitution earlier was therefore not proved by "0 draws
  refused" but is not refuted either -- Latin text looking identical is what a
  correct HUD-font swap SHOULD look like, since the two fonts differ in their
  button glyphs, not their letters.
* `CreateFileMappingA` being outside the shared resolver is still a real gap,
  but it is not this one's cause.

## The actual lesson, twice over

`CLAUDE.md`: *a discriminator must be run against BOTH classes before you trust
it -- not reasoned about, run.* This one was run, and it still lied, because
the negative case was chosen without checking what the test surface was
already made of. The check that would have caught it costs nothing: **before
trusting a substitution test, confirm the thing on screen is drawn by the thing
being substituted.** Both mistakes here -- the first false positive and then
the false negative that "corrected" it -- come from skipping that.

### Note (2026-08-13)
The metrics this feature publishes would have been placed MIRRORED. C171: a font atlas's t is measured from the BOTTOM of the image (decoded row = height - t*height), settled by counting inked pixels inside the 166 glyph rects of x2f_med_pc -- the flipped reading accounts for all 17,944 inked pixels in the atlas, the as-is reading for 8,315 of them. tools/make_button_font.py measures cells top-down and writes t = y0/atlas_height, so every glyph it publishes points at the mirror image of the cell it measured. That is very likely why replacing a font .igb changed nothing visible: the art was there and the rect pointed at empty space above it.
