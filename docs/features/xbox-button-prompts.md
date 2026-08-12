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
