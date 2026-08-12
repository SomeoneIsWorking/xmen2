# Xbox button prompts

> **THE STATED SOURCE FOR THIS FEATURE IS WRONG.** `x2f_hud_xbox.igb` does not
> contain Xbox button glyphs. Measured, below. Nothing can be built on it until
> the real source is found, and no amount of screen-hunting would have shown
> that -- the substitution was a no-op by construction.


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

## MEASURED: substitution DOES work (issue #60, after a false negative)

Dropping the Xbox HUD font over `textures/fonts/X2F_hud_PC.igb` gives a run
with both files replaced, 0 draws refused, and a correct-looking caption.

That on its own proves nothing, and a first attempt to check it produced a
FALSE NEGATIVE that is written up in issue #60: the "wrong font" chosen as the
control was the very font the test caption was already drawn in, so nothing
changed and the mechanism was wrongly declared broken. Repeated with
`x2f_big.igb` over the other three fonts, the caption's second line went from
**"GREENLAND" to "G"** -- the wrong glyphs drawn through the wrong metrics.

**So `X2_ASSETS` font replacement reaches the renderer.** And Latin text looking
unchanged under the Xbox HUD font is what a CORRECT swap should look like: the
two fonts differ in their button glyphs, not in their letters.

## What is NOT done, and the one thing that blocks it

**Correction:** the 31-name list this paragraph was based on came from an
instrument that watched only `CreateFileA`; routing the CRT's `fopen` through
the same resolver took a run to 352 names. `joy1..4.png` still do not appear,
but that is now a weaker statement than it was, and the mapping path is still
uncovered.

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


## MEASURED: `x2f_hud_xbox.igb` contains no button glyphs

`README.md` says feature 3 is "glyphs from `x2f_hud_xbox.igb` replacing the PC
`Texs/joy1..4.png`". The asset does not support that.

* The **glyph textures are byte-identical**. Decoding both fonts to PNG at every
  mip level and comparing: `256x256`, `128x128` and `64x64` all differ in **0
  bytes of 262,400 / 65,664 / 16,448**. The Xbox HUD font's atlas is the PC HUD
  font's atlas.
* The platform variants differ only in **size of metadata**: `x2f_hud` 93,252
  bytes, `_gc` 93,284, `_ps2` 93,288, `_xbox` 93,288 -- a few dozen bytes apart,
  which is names and headers, not different pictures. The same pattern holds
  across every font family in the set (`x2f_med`, `x2f_thin`, `font_xmen_digital`).
* The Xbox `assetsfb.wad` has **2,520 entries and not one button, joy, glyph,
  pad or prompt asset**. The only matches for those words in the whole index are
  the HUD fonts themselves and four character models belonging to Heather and
  James Hudson.

So substituting that font can never change a button prompt, and the earlier
substitution run -- both files replaced, 0 draws refused, Latin text unchanged
-- was a no-op *by construction* rather than a success or a failure. That is
also why hunting for a screen that draws a prompt would have wasted the effort:
there was nothing different to see.

## What this leaves

The mechanism is done and verified independently of this (C162): `X2_ASSETS`
replacement reaches the renderer, proven by a substituted font changing drawn
glyphs. What is missing is the **source art**, and finding it is an RE question
about the XBOX build, not about this host:

1. Where does the Xbox build's UI get a button prompt from? `default.xbe` is
   already lifted by this repo's own Xbox path (`xbox/`, `tools/xbox_relift.sh`),
   so its UI code can be read the way `XMen2.exe`'s controller code was.
2. Is a prompt a TEXTURE at all, or a character drawn from a font whose atlas
   is shared and whose *metrics* differ? The `.xmlb` metrics files were NOT
   compared -- only the textures were -- and that is the cheapest next check,
   since the whole platform difference has to live in the ~36 bytes that differ
   plus the metrics file.
3. The ISO holds assets outside `assetsfb.wad`; only `textures/` and `ui/` were
   ever extracted from it here.

Check (2) first. It is minutes of work and it is where the evidence points.
