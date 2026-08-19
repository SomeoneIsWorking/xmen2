# Xbox button prompts

> **SOURCE FOUND AND SEEN.** The Xbox button art is in `x2f_med_xbox.igb`:
> a d-pad cross, shoulder/trigger shapes, gold face buttons and coloured
> button squares, in atlas cells the PC medium font does not have.
>
> **THE STATED SOURCE IS WRONG, AND THE REAL ONE IS FOUND.**
> `x2f_hud_xbox.igb` is not it -- the Xbox build does not even load that file.
> The fonts that actually differ between the platforms are **`X2F_med_XBOX`**
> and **`X2F_thin_XBOX`**, and their art differs from the PC's by 66.5%.


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
the distinct set and reports it (`X2_FILES=1` lists them live). A run to
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

### Check (2) is done, and it closes the question

The metrics were compared. `ui/fonts/x2f_hud.xmlb`, `x2f_hud_gc.xmlb` and
`x2f_hud_xbox.xmlb` are **byte-identical -- the same md5, 27,082 bytes each**.
Only the PS2 variant differs (26,942 bytes).

So `x2f_hud_xbox` is the same font as `x2f_hud` in **both** the texture and the
metrics. The "Xbox HUD font" is not an Xbox-specific font at all; the platform
suffix names which build ships it, not different art. There is nothing in it to
take.

**Feature 3 as written in `README.md` cannot be built, and this is a disproof
rather than a difficulty.** The next work is to find how the Xbox build draws a
button prompt at all -- most cheaply by reading its UI code, since this repo
already lifts `default.xbe` (`xbox/`, `tools/xbox_relift.sh`) -- or to accept
that the prompts are drawn from art that is not in `assetsfb.wad` and extract
the rest of the ISO. Either way it starts from the Xbox build, not from this
host, and not from a font that turned out to be a copy of the PC one.


## FOUND: the real source is X2F_med_XBOX / X2F_thin_XBOX

The font SET files settle it. `ui/fonts/fonts_pc.xmlb` and
`ui/fonts/fonts_xbox.xmlb` are 392 and 396 bytes, and their string tables
differ in exactly two entries:

    pc  : ... X2F_med_PC   ... X2F_thin_PC   ... X2F_big ... X2F_hud_PS2 ... font_XMEN_digital_XBOX
    xbox: ... X2F_med_XBOX ... X2F_thin_XBOX ... X2F_big ... X2F_hud_PS2 ... font_XMEN_digital_XBOX

Two things fall out of that:

1. **Both platforms use `X2F_hud_PS2` for the `hud` slot.** So the Xbox build
   never loads `x2f_hud_xbox.igb` at all, and neither does the PC one -- which
   is why the PC install's `x2f_hud_pc.igb` has the same md5 as the ISO's
   `x2f_hud_ps2.igb`. Every substitution aimed at the HUD font was aimed at a
   file nothing reads.
2. **The platform difference lives in `medium` and `thin`.** Comparing the art
   with the fixed extractor: the PC's `x2f_med_pc.igb` and the Xbox's
   `x2f_med_xbox.igb` differ in **174,548 of 262,400 bytes (66.5%)** at
   256x256. That is different pictures, not different encoding.

Note also that `x2f_med_pc.igb` decodes to **1** igImage and `x2f_med_xbox.igb`
to **9** -- a difference the old extractor could not have shown, since it
emitted one PNG per mip size and kept only the last.

### The next step, now concrete

Replace `X2F_med_PC` (and `X2F_thin_PC`) with the Xbox files through
`X2_ASSETS`, and look at menu text -- `X2F_med` is the medium UI font, so it is
on screen constantly and needs no special screen to verify. The mechanism is
already proven (C162), the target is now known, and the check is the one this
document has been missing: put the Xbox art in and see the glyphs change.


## The glyphs, seen

Decoding both medium fonts to PNG and looking at them (the check that should
have come first, before any substitution run):

* `x2f_med_xbox.igb` -- same Latin letters as the PC font, plus a block of
  button art below them: a **d-pad cross**, dark **shoulder/trigger** shapes, a
  row of **gold face buttons**, and a row of **coloured button squares**.
* `x2f_med_pc.igb` -- the same letters, and those rows are absent.

That is the discriminator run in both directions, and it is what the whole
feature was looking for. C166.

## The remaining question, which is about STRINGS not art

The PC build's prompt reads **`[ENTER] CONTINUE`** -- seen on the Danger Room
dialog. That is literal letters from the strings table, not a glyph cell. So
swapping the font alone will not turn it into a button: the Xbox build must
emit a different CHARACTER there, and the character-to-cell mapping lives in
the font's `.xmlb` metrics plus whatever the Xbox strings table contains.

So the next step is the strings, not another font run:

1. Find the codepoints the Xbox metrics map to those new cells
   (`ui/fonts/x2f_med_xbox.xmlb` against `x2f_med_pc.xmlb`).
2. Find what the Xbox strings table puts where the PC one puts `[ENTER]`.
3. Decide whether the port substitutes the string, the font, or both -- and
   whether it should follow the CONNECTED CONTROLLER rather than the platform,
   which is what a modern port would do and what makes this feature worth
   having now that hotswap works.


## Two more measurements, and what is left

**The metrics are identical.** `ui/fonts/x2f_med_pc.xmlb` and
`x2f_med_xbox.xmlb` are byte-identical (27,589 bytes, same md5, and the same as
the PC install's own copy). So the same codepoints map to the same cells on
both platforms -- the Xbox atlas simply has button art in cells where the PC
atlas does not. **That means the font swap alone is sufficient art-wise**: if
something emits those codepoints, the Xbox font draws buttons.

**Nothing emits them.** With a controller connected (`X2_VIRTUAL_PAD=1`) AND
the Xbox medium font substituted, the difficulty dialog still reads
`[Esc] Back` and `[Enter] Select`.


## Correction: it is NOT the strings table. The label is BUILT AT RUN TIME.

The paragraph this replaces said the prompts were "keyboard strings from the
strings table" and made the feature a data question. That was wrong, and the
measurement that shows it is blunt: **`[Enter]` does not exist anywhere in the
install.** A scan of all 13,382 files / 2,368.5 MB of the PC install for
`[Enter]`, its UTF-16 form, `Enter]` and `ENTER]` returns **0 hits** (blind
spot: packed containers are not decompressed by that scan). Neither does the
exe or any DLL contain the bare word `Enter`, `Escape` or `Backspace`.

`Data/strings.engb` is also **byte-identical between the Xbox WAD and the PC
install** (53,487 bytes, md5 `ee1a8c03a0dfebc58ee74df43e98f13a`), so the Xbox
build does not get button prompts from there either. `strings_svs.engb` is the
only strings file that differs at all, and its single difference is an Xbox
Live sentence appended to a television-standard warning -- nothing to do with
button art.

### The real mechanism, read out of XMen2.exe

`FUN_00619e30(action)` returns the on-screen label for a bound action:

1. `FUN_00619c40(action)` maps the action to a binding index; `-1` -> `" "`.
2. `FUN_006294b0(bindIdx, slot, &devkind, &code)` reads the binding table.
   Each binding row is four slots of three dwords (48 bytes); `+4` is the
   device kind and `+8` the code. **The slots are tried in the order 2, 0, 1,
   1** and the first with a non-zero device kind wins.
3. `FUN_006281f0(devkind, code)` turns that into a name.
4. `sprintf(0xa68c18, "[%s]", name)` -- the literal `[%s]` at `0x6a4e64`, the
   only such format in the exe, referenced only from here. All four slots
   empty -> the literal `[???]` at `0x6a4e6c`.

`FUN_006281f0` is the name table, and it is per device kind:

| devkind | source of the name |
| --- | --- |
| 1 (keyboard) | `this+0x289c + code*0x100` -- 256 entries of 256 bytes |
| 2 (mouse) | fixed strings at `this+0x1289c…0x12950` |
| 3..0xc (pad) | code 1..0x10 axis via `this+0x1296e`, 0x11..0x14 POV via `this+0x129aa`, 0x15..0x31 button via `this+0x1298c` with `sprintf("%d", code-0x14)` |

The three pad formats are loaded from the localisation table `igct.bnx` by
`FUN_00629210` through the keys `GamepadAxis`, `GamepadPoV`, `GamepadBtn`, and
installed by the setter `FUN_00627a00`. In English they are:

```
GamepadAxis=Axis %s
GamepadPoV=PoV %s
GamepadBtn=Btn %s
```

The keyboard table is filled by the same `FUN_00629210`, in a 256-iteration
loop: `FUN_00627a50(scancode)` supplies the name (the DirectInput object name),
and where that is empty it falls back to the character from the Win32
key-mapping call at `[0x67f24c]` (with a `0xb4 -> 0x27` fixup), and to `"???"`
where even that is zero. **So `Enter` and `Esc` are strings this port itself
hands the game, through DirectInput.**

### What this changes

Two things, and both are better than the string-substitution plan.

**The device switch is already in the game.** Slot 2 is consulted before slots
0 and 1, so a populated pad binding wins over the keyboard binding and the
label follows the pad automatically. Nothing has to detect "a controller is
connected" at the label site -- hotswap (C161) populating the pad slot is the
whole switch. That is the behaviour the user asked for ("switch prompts based
on hotswap"), and it needs no new policy layer.

**The glyph goes in the NAME, not the string table.** With the Xbox font
substituted, an override of `FUN_006281f0` that returns the Xbox glyph
codepoint for `devkind >= 3, code 0x15..0x31` makes the label render as a
button. `[%s]` still wraps it, which is what the Xbox build shows too.

### Left to do

1. Override `FUN_006281f0` natively for the pad device kinds, returning the
   one-character glyph string. Contract, from the disassembly: `__thiscall`,
   `RET 0x8`, arg0 = devkind, arg1 = code, returns `char *` to a static buffer
   (the exe uses `0xa6aec8` / `0xa6ae78` / `0xa6ae28` per kind).
2. Map code 0x15..0x31 to the Xbox glyph codepoints in `x2f_med_xbox.igb`.
   The metrics being identical (above) means the codepoint is all that is
   needed.
3. Populate the pad binding slot on hotswap -- which is feature 2, the Xbox
   default mapping table. Until that lands the pad slot is empty on a fresh
   profile and the keyboard label still wins, correctly.


## The button art is NOT a codepoint. Correcting step 2 above.

Step 2 said "map code 0x15..0x31 to the Xbox glyph codepoints in
`x2f_med_xbox.igb`". Measured, that plan is wrong: **there are no such
codepoints.**

Method, and what a negative would have looked like. Both atlases were decoded
to PNG (`tools/extract_font_igb.py`, fixed after I045 so it emits every image
rather than the last per mip size), the glyph table was parsed out of
`x2f_med_pc.xmlb` (256 `<glyph>` nodes, each with `num` and the UV rect
`s,t,s2,t2`), and every glyph's rect was compared pixel-for-pixel between the
PC and Xbox atlas:

```
256 glyphs: 90 with an empty rect (not comparable), 166 identical, 0 DIFFER
```

Zero of the 166 comparable cells differ, so no codepoint renders differently on
Xbox. That is not "the tool found nothing" -- the same run shows the atlases
DO differ, in a band at x 2..240, y 202..242 (3,765 of 65,536 pixels), and that
band visibly holds the d-pad, the coloured A/B/X/Y buttons, the L/R triggers,
the black and white buttons and the sticks. The art is there. It is simply
**outside every glyph rect**: the highest `t2` in the table is y=188, and no
glyph rect intersects y 202..242. Checked across all nine metric variants in
the WAD (`x2f_med{,_gc,_pc,_pc_hd,_pc_ws,_ps2,_xbox,_xbox_hd,_xbox_ws}.xmlb`);
the largest `t2` any of them reaches is 199, still short of the band.

So the Xbox build does not draw its button prompts as text through the font.
The font texture doubles as a UI sprite sheet and the buttons are drawn as
textured quads with explicit UVs by UI code that has nothing to do with
`FUN_006281f0`. Substituting the font can therefore never produce a button
glyph in a `[%s]` label, no matter what string is fed to it -- which also
explains the earlier run where the Xbox font was substituted, a pad was
connected, and the caption was unchanged.

### What this leaves standing, and what it kills

Still true and still the mechanism (C167): the label is built at run time from
the DirectInput object name, and slot 2 -- the pad slot -- is consulted first,
so hotswap already decides which device the label describes.

Dead: font substitution as the delivery route for the art.

The remaining route is to draw the sprite ourselves. The port has the atlas
(the band above), it knows the UV of each button in it, and it knows when a pad
is connected. What it does not yet have is the Xbox UI code that places those
quads, and that has not been located. **That is the next RE step, and it is the
honest status: the art is found, the label mechanism is understood, and the
bridge between them is not.**


## The Xbox build has no `[%s]` label mechanism at all

Checked directly in `scratch/xbox_iso/default.xbe` (5,726,208 bytes, strings
plainly visible -- `XMen` x6, `Select` x7, `%d` x474, `.igb` x11, so this is not
a packed-strings false negative):

| needle | count in default.xbe |
| --- | --- |
| `GamepadBtn` / `GamepadAxis` / `GamepadPoV` | 0 / 0 / 0 |
| `[%s]` | 0 |
| `[???]` | 0 |
| `MouseBtn` | 0 |
| `Controls\Player%d\%s` | 0 |

So the entire "format the device object name into `[%s]`" system is **PC-only**
-- added for the PC port, which is exactly what a port needs and a console does
not. The Xbox never renders `[Btn 1]`, and there is no Xbox routine to port.
The nearest thing the XBE has is a debug binding-name table (`START`, `SELECT`,
`DPAD_LF/RT/DN/UP`, `L1/L2/R1/R2`, `SQUARE/CIRCLE/TRIANGLE`, `WHITE`, `BLACK`,
`LEFT TRIGGER`, `LEFT STICK CLICK`, `MCHEAT`, `SCREENGRAB`), whose PS2 face
names give away what it is.

The UVs are not constants in the PC binaries either. Searching all 21 exe/dll
files for the measured cell coordinates as float32 and float64 -- `0.785156`,
`0.871094`, `0.945312`, `0.007812`, `0.078125`, `0.875` -- the three
distinctive ones get **zero hits**; only the unremarkable `0.875` and
`0.078125` appear, and those are ordinary constants. (A UV divided from pixel
coordinates at run time would leave no constant, so this alone would not be
proof -- it is corroboration, not the argument.)

## Where the art actually is, measured to the pixel

Segmenting the Xbox atlas band by alpha gives two rows of cells. Marked
`XBOX-ONLY` where the cell differs from the PC atlas:

```
row y201-223                          row y224-242
  x  2- 20  XBOX-ONLY                   x  2- 20  XBOX-ONLY
  x 24- 42  XBOX-ONLY                   x 24- 42  XBOX-ONLY
  x 45- 65  same as PC                  x 46- 64  XBOX-ONLY
  x 73- 81  same as PC                  x 68- 86  XBOX-ONLY
  x 91-107  same as PC                  x 90-108  XBOX-ONLY
  x110-220  same as PC                  x112-130  XBOX-ONLY
  x221-241  same as PC                  x132-176  XBOX-ONLY  (wide: L/R pair)
  x243-253  same as PC                  x179-195  same as PC
                                        x200-218  XBOX-ONLY
                                        x222-240  XBOX-ONLY
                                        x243-251  same as PC
```

Eleven Xbox-only cells, which is the right count for d-pad, A, B, X, Y, black,
white, the two triggers and the two sticks. Note the band is populated in the
PC atlas too -- so this region is shared sprite space in both builds, not an
Xbox addition, and only the button cells differ.

## Implemented delivery: unused codepoints through the original text path

The game's text path is the only thing that draws a `[%s]` label, so the art
reaches it as a glyph. The implementation is now:

**90 of the 256 glyphs have an empty rect** -- codepoints the font does not use.
`X2_ASSETS` already substitutes both halves of a font (the `.igb` atlas and the
`.xmlb` metrics; C162 proved a substituted font reaches the renderer, by making
`GREENLAND` render as `G`). So:

1. `assets/buttons/glyphs.json` is the one ordered codepoint authority;
   `tools/pad_glyph_manifest.py` validates its eleven SVGs and generates
   `pad_glyph_codes.h` for C. `tools/make_pad_font.py` consumes the same
   manifest to rasterise those SVGs into the PC atlas's measured empty band at
   bytes `0x80..0x8a`, so Python and the runtime cannot drift.
2. `tools/prepare_native_assets.py` fingerprints both shipped PC font files,
   the SVGs and the builder. The zero-argument `./run.sh` builds the verified
   derived pack on a miss and reuses it on a hit, then enables the hook only
   with that pack. No Xbox-owned art is copied or committed.
3. `src/native/pad_glyphs.c` overrides `FUN_006281f0` at its measured
   `__thiscall`, `RET 0x8` contract. It maps the physical DirectInput codes
   already established by `dinput_pad.c`: buttons `0x15..0x1c`, Z+/Z- (`5/6`)
   to LT/RT, and POV `0x11..0x14` to d-pad. Back/Start follow the game's button
   order, not the font's order.
4. The hook fires only for the gamepad slot named by `devkind 3..0xc` when SDL
   classifies its connected device as Xbox 360/One. Keyboard, PlayStation,
   generic controllers, unknown codes and LS/RS super-call the retained
   recompiled body. LS/RS have no authored SVG, so retaining `Btn 9/10` is an
   explicit fallback rather than a blank glyph.

This stays entirely inside the game's own text pipeline -- no new draw path and
no renderer special case. `tests/test_pad_glyphs.c` calls the exact shipping
wrapper and checks its stack pop, returned guest pointer, bytes, Xbox/non-Xbox
split, unsupported-code deferral and pack-disabled gate. A real default launch
independently proves both derived font files are opened through `X2_ASSETS`.

The remaining user-visible prerequisite is action assignment. A fresh-profile
virtual-pad run opens and reads the pad but creates no pad action bindings, so
`FUN_00619e30` never asks the name function for a pad label. The delivery path
is implemented and verified; the prompt appears only after the Xbox-default
mapping is recovered and installed through the mapping UI feature below.


## MEASURED 2026-08-18: the dialog prompt does NOT come through FUN_00619e30

With the pad working end to end (#82 fully fixed) and the derived font pack
active, the tutorial dialog still reads `[ENTER] CONTINUE...`. The counters say
why, and they say it only because they carry denominators:

    Xbox prompt names: 0 glyph(s), 6491 original name(s); font pack enabled
    Xbox prompt rows:  0 label read(s) -- 0 answered with the row's pad
                       binding, 0 had none

* `FUN_006281f0`, the naming boundary, ran **6,491 times** -- so labels ARE
  being built constantly -- and not one call had a gamepad device kind.
* `FUN_006294b0`, the binding reader `FUN_00619e30` uses to choose which slot a
  label describes, ran **0 times**.

So `FUN_00619e30` is not what produces this prompt, and the plan recorded above
-- "slot 2 is consulted first, so a populated pad binding wins and the label
follows the pad automatically" -- is wrong for the path that actually draws it.

The next step is therefore to find the OTHER caller: what calls `FUN_006281f0`
6,491 times in a run that shows one dialog, and with which device kind. That is
a caller census on the real run, not more reading -- `src/native/pad_glyphs.c`
already counts every call and can record its callers.

The `FUN_006294b0` override added alongside this note is still correct for the
path it covers (a row with a pad binding names it, whatever slot it sits in,
while an Xbox pad is connected) and costs nothing where it does not fire. It is
kept because it is the right behaviour and because its zero count is the
measurement above.


## SHIPPED: the glyphs render in game, and the blocker was ours

The prompt bar of the main menu, with a pad connected and the derived pack
active, now draws

    [B] BACK        [A] SELECT

with the real Xbox button art -- `scratch/shots/ship.png`, zoomed in
`scratch/shots/glyph_zoom.png`. That is the in-game capture this document has
been waiting for, and the tutorial dialog shows the same thing in place of
`[ENTER] CONTINUE...`.

**The cause of the years-long "the game will not draw our codepoint" was a unit
error in this port's own font builder.** XMen2.exe's glyph metrics are PIXELS:
`A` is `width="14" height="13"` and its UV rect is exactly 14x13 of the 256x256
atlas. `tools/make_pad_font.py` divided the 18px cell by the font's line height
and published `height="0.9"`, `horizadvance="0.95"`. The game drew those glyphs
faithfully, at nine tenths of a pixel.

That is why every earlier theory fit the evidence and none of them was right.
An invisible glyph looks identical to a glyph the renderer skipped, so the
investigation went through the codepoint (0x80 -> 0x01 -> 0x7b, all blank), the
font choice (only `x2f_med_pc` is RGBA8888; the other three the game loads are
pixel format 15 and the builder refuses to re-encode them), and the printable
range -- while a stock glyph, `Q`, drew perfectly through the same path the
whole time. The discriminator that finally separated them was reading the
published metrics beside the font's own, not another run.

`make_pad_font.py` now refuses to publish a glyph whose height falls outside the
range the font's own glyphs use, and says so:

    metrics units ok: new glyphs are 18 tall, the font's own run 3..22

### What is measured, and what is not

* Measured: the menu prompt bar and the conversation prompt, both drawing the
  correct button for the bound action (`Back` shows B, `Select` shows A),
  through the retained `FUN_006281f0` boundary, on the SHIPPING codepoints
  0x80.. -- no printable-range restriction exists.
* Measured: with a pad connected the prompt follows the pad rather than the
  keyboard, because the label-selection override answers with the row's pad
  binding (7,873 of 7,873 label reads in one run).
* NOT measured: any prompt on a screen whose text uses `X2F_big`,
  `X2F_hud_PC` or `font_XMEN_digital`. Those three are pixel format 15 and the
  builder refuses them, so a prompt drawn in one of them would still be blank.
  Whether any prompt uses them is unknown.
* NOT measured: real hardware. Everything here is the synthetic pad.


## Three corrections after the first capture

The first in-game capture was `[B] BACK`, and all three of these were wrong in
it. None was visible from that screenshot, which is the point.

**1. The glyphs were upside down.** A `B` mirrored still reads as a `B`, and so
do `A`, `X` and `Y` -- every glyph the menu happens to show is near-symmetric,
so the capture looked correct. The discriminator has to be an ASYMMETRIC glyph:
`X2_PAD_GLYPH_PROBE=0x84` forces LB, which carries an `L`, onto every prompt,
and it drew as `Γ`. Cause: `decode_atlas` returns a BOTTOM-UP buffer while the
glyph UVs are top-down into the real texture -- confirmed by reading a stock
`A` out of the decoded atlas both ways, which gives an upside-down A one way
and an unrelated stroke the other. `row_to_t` already accounted for the row
order, so the cell selection was right and only the art within it was mirrored.
`blit` now writes the source rows reversed, and the selftest uses a cell whose
rows DIFFER so it can see the flip -- the old one used a uniform cell and could
not have.

**2. The glyph sat five pixels above the line.** `baseline` is not per-glyph
geometry: every drawing glyph in this font uses the same value (11 of 166
sampled, 83 agreeing), so it is the ascent -- the distance from the glyph box
top down to the text baseline. Deriving `CELL - 2` from the letters'
height/baseline relationship lifted ours. The builder now takes the font's own
value, by majority of the glyphs that draw.

**3. The square brackets are wrong around a picture of a button.** `[ENTER]`
reads as a key; `[A]` does not -- no console prompt draws brackets round its
button art. A third override on `FUN_00619e30` removes them, and ONLY when the
composed label is exactly one of this port's glyphs in brackets: a keyboard
name, a one-character keyboard name like the `A` key, and the unmapped `[???]`
all keep theirs. The test covers all four and was checked against two
mutations -- never stripping, and stripping anything short.

The result is `B BACK` / `A SELECT` with the buttons upright, on the baseline
and unbracketed: `scratch/shots/final_prompt.png`.
