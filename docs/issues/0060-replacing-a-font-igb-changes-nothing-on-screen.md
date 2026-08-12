---
id: 60
title: Replacing a font IGB changes nothing on screen -- the loose textures/fonts/*.igb are not where the drawn glyphs come from
symptom: "assets: REPLACED textures/fonts/X2F_hud_PC.igb ... and the rendered caption is byte-identical to the control"
tags: pc,native,assets,fonts,graphics,glyphs,dead-end
status: open
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
