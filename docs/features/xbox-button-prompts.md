# Xbox button prompts

Feature 3 of the three (`README.md`): the Xbox build's authentic button glyphs
standing in for the PC build's `Texs/joy1..4.png`, which are just the digits
1--4.

## What exists now

**The assets.** `$XBOX_ISO` is extracted to `scratch/xbox_iso/` and the Xbox HUD
font is decoded: `scratch/prompts/xbox/x2f_hud_xbox_<size>.png` for every mip
level, alongside the PC `x2f_hud` for comparison, and the four PC
`joy1..4.png`. The font metrics are beside them on the ISO
(`assets/ui/fonts/x2f_hud_xbox.xmlb`), which is what says where each glyph sits
in the atlas.

**The mechanism.** `X2_ASSETS=<dir>` redirects any file open whose relative
path exists under `<dir>` (`src/native/kernel32.c`). It is general on purpose
-- a texture pack, a translation or a debugging swap all want the same thing,
and a special case for four files would have to be replaced by this the first
time anyone wanted one of those. The install is never written and never read
for a replaced name.

Measured on a real run: a magenta stand-in dropped at
`scratch/assetpack/texs/pause.png` produced

    assets: REPLACED "texs\pause.png" with scratch/assetpack/texs/pause.png
    files: 36 open call(s) over 30 distinct name(s); 0 failed
           X2_ASSETS=scratch/assetpack -- 1 name(s) were replaced from it

and the report says `NONE` when a pack matches nothing, so a pack that is
silently doing nothing is distinguishable from one that is working.

**What the game opens**, which was unanswerable before: `CreateFileA` now keeps
the distinct set and reports it (`X2_LOG_FILES=1` lists them live). A run to
gameplay opens 31 names, and the HUD textures are among them by name --
`texs\cursor0.png`, `texs\teamm.png`, `texs\power_frame.png` and the rest, read
out of the install directly rather than out of a package.

## What is NOT done, and the one thing that blocks it

`joy1..4.png` **were not opened in any run so far.** They are the player-number
icons, and nothing in the scripted route -- menu, cutscene, level, death dialog,
menu -- reaches the screen that uses them. Until a run opens them, replacing
them cannot be verified, and a replacement nobody has seen the game load is
exactly the "faked step" the RE frontier exists to prevent.

So the next work item is not image editing. It is: **get a run to a screen that
draws button prompts**, most likely the player-join or controller screen, which
is also the first place a second controller matters. That now has a chance of
working, because the game can finally see a gamepad (C160) and one can be
plugged in mid-run (C161).

After that:

1. Read `x2f_hud_xbox.xmlb` for the glyph rectangles rather than eyeballing the
   atlas.
2. Cut the four glyphs the prompts need and write them at the sizes the PC
   files use.
3. Verify by running with `X2_ASSETS` and looking at the frame -- and by the
   redirect line, so "it looks the same" cannot be mistaken for "the
   replacement did not load".
