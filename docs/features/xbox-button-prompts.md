# Xbox button prompts

> **SOURCE FOUND AND SEEN.** The Xbox button art is in `x2f_med_xbox.igb`:
> a d-pad cross, shoulder/trigger shapes, gold face buttons and coloured
> button squares, in atlas cells the PC medium font does not have.
>
> **THE STATED SOURCE IS WRONG, AND THE REAL ONE IS FOUND.**
> `x2f_hud_xbox.igb` is not it -- the Xbox build does not even load that file.
> The fonts that actually differ between the platforms are **`X2F_med_XBOX`**
> and **`X2F_thin_XBOX`**, and their art differs from the PC's by 66.5%.


Feature 3 of the three (`README.md`): port-authored SVG equivalents of the Xbox
controls standing in for the PC build's text labels and `Texs/joy1..4.png`
player-number images.

## What ships now

Prompt delivery no longer edits or derives a game font.
`tools/render_prompt_glyphs.py` rasterises the shared `port-assets` SVGs at
build time into a generated, port-owned GPU atlas. The input-name and prompt
label overrides publish private byte codepoints `0x80..0x93` according to the
active assigned input source. After the retail loader has populated a font
record, the port publishes only width, height, advance and baseline metrics for
unused private cells in memory. It writes no prompt pixels or UVs into a font
record or an on-disk font asset.

The retail text measurer and layout code position those cells. At the RE'd
Alchemy non-indexed text-batch boundary the port harvests their quads with the
exact UI transform and batch colour, queues the native art, and calls the
retail glyph emitter with a zero-area rectangle. The collapsed call preserves
the engine's vertex/batch/finalizer behavior without sampling a stock-font
pixel. Ordinary text and the stock batch/state-finalizer paths continue
through their supercalls. Whole strings fall back before interception if any
codepoint, colour read, or queue reservation is unavailable.

The old `tools/make_pad_font.py` builder and derived prompt-font pack have been
removed. The default launcher neither builds nor opens a prompt font.
`X2_ASSETS` remains the general replacement route used for the derived pause
menu row and for user mods; it no longer delivers or patches prompt glyphs.

The 3840x2160 keyboard capture
`scratch/screenshots/svg-final-unbounded.png` visibly shows the native SVG
`ENTER` keycap around retail letters; its log records 1,188 quads in 99
semantic batches with zero refusals or desyncs.

The separate pure-controller run closes the shape that keyboard evidence
could not cover. `scratch/logs/svg-pad-final-unbounded.log` records 76 metric
cells published across four font records (two occupied cells left alone),
1,073 one-codepoint controller strings, 1,073 harvested/submitted quads, and
1,073 matching nested Alchemy finalizers. Across 14,197 non-indexed draws and
99,936 total state-finalizer calls it reports zero desync, unavailable-byte,
colour, capacity, transform, cross-context, GPU, and unfinalized-boundary
refusals. `scratch/screenshots/svg-pad-unbounded.png` visibly shows the native
green A icon beside `CONTINUE...`. Both runs were headless, silent, and used
the unbounded scheduler. Issue #121 preserves why retaining a collapsed retail
emitter call is required for this one-glyph case.

## Historical investigation: asset substitution

The sections below preserve the evidence that established the label-selection,
font and art contracts. Derived-font generation and `X2_ASSETS` prompt
instructions in this chronology describe the retired delivery route, not the
current launcher.

**The assets.** `$XBOX_ISO` is extracted to `scratch/xbox_iso/` and the Xbox HUD
font is decoded: `scratch/prompts/xbox/x2f_hud_xbox_<size>.png` for every mip
level, alongside the PC `x2f_hud` for comparison, and the four PC
`joy1..4.png`. The font metrics are beside them on the ISO
(`assets/ui/fonts/x2f_hud_xbox.xmlb`), which is what says where each glyph sits
in the atlas.

**The historical mechanism.** `X2_ASSETS=<dir>` redirects any file open whose
relative path exists under `<dir>` (`src/native/kernel32.c`). It is general on
purpose -- a texture pack, a translation or a debugging swap all want the same
thing, and a special case for four files would have to be replaced by this the
first time anyone wanted one of those. The install is never written and never
read for a replaced name.

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

**So the historical `X2_ASSETS` font replacement reached the renderer.** Latin
text looking unchanged under the Xbox HUD font was what a correct swap should
look like: the two fonts differed in their button glyphs, not their letters.

## Historical blocker at that point

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

The next work item at that point was not image editing. It was: **get a run to a screen that
draws button prompts**, most likely the player-join or controller screen, which
is also the first place a second controller matters. That now has a chance of
working, because the game can finally see a gamepad (C160) and one can be
plugged in mid-run (C161).

After that:

1. Read `x2f_hud_xbox.xmlb` for the glyph rectangles rather than eyeballing the
   atlas.
2. Cut the four glyphs the prompts need and write them at the sizes the PC
   files use.
3. The planned verification was a run with `X2_ASSETS`, checking both the frame
   and redirect line so "it looks the same" could not be mistaken for "the
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

1. Where does the Xbox build's UI get a button prompt from? This remains a
   binary-evidence question, not a product execution path.
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
rather than a difficulty.** The remaining evidence had to come from the Xbox
binary or assets, not from this host and not from a font that turned out to be
a copy of the PC one.


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

## Historical delivery: derived font pack (removed)

The first working delivery used empty private codepoints in a derived copy of
the PC medium font. It proved the label-selection, codepoint, art and metrics
contracts, but it was removed because a game font should not own the port's
prompt renderer. The font-pack details below are historical evidence, not
current build or launch instructions.

**90 of the 256 glyphs had an empty rect** -- codepoints the font did not use.
At that stage `X2_ASSETS` substituted both halves of the derived font (the
`.igb` atlas and `.xmlb` metrics; C162 had proved a substituted font reached the
renderer by making `GREENLAND` render as `G`). The delivery was:

1. `assets/buttons/glyphs.json` was the ordered codepoint authority;
   `tools/pad_glyph_manifest.py` validated its SVGs and generated
   `pad_glyph_codes.h`. The now-removed `tools/make_pad_font.py` consumed that
   manifest to rasterise them into the PC atlas's empty band.
2. `tools/prepare_native_assets.py` formerly fingerprinted the shipped font,
   SVGs and builder, and the launcher formerly cached the derived pack. It now
   prepares only the pause-menu asset pack; prompt generation belongs to
   `tools/render_prompt_glyphs.py` and the native atlas.
3. `src/native/pad_glyphs.c` overrides `FUN_006281f0` at its measured
   `__thiscall`, `RET 0x8` contract. It maps the physical DirectInput codes
   already established by `dinput_pad.c`: buttons `0x15..0x1c`, Z+/Z- (`5/6`)
   to LT/RT, and POV `0x11..0x14` to d-pad. Back/Start follow the game's button
   order, not the font's order.
4. The hook fires only for the gamepad slot named by `devkind 3..0xc` when SDL
   classifies its connected device as Xbox 360/One. Keyboard, PlayStation,
   generic controllers and unknown codes call the retained runtime-translated
   body. LS/RS use the authored shared `port-assets` glyphs and the same
   manifest/runtime mapping as every other supported physical code.

The selection hook remains current. Pixel delivery does not: prompt quads now
leave the game's text pipeline at its evidenced batch boundary and are drawn by
the native GPU atlas. `tests/test_pad_glyphs.c` still exercises the shipping
selection wrapper; font-pack loading is no longer part of the contract.

The action-assignment prerequisite is also closed. A fresh profile installs the
canonical 22-row controller preset, including Power and TargetLock/Use Health
Pack, so `FUN_00619e30` can select the pad binding without requiring a visit to
the mapping UI.


## MEASURED 2026-08-18: the dialog prompt did not exercise FUN_00619e30

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

That run proved only that the conversation's `$MENU_ACCEPT` control was drawn
by its dedicated conversation path, not that action labels bypass the builder.
The completed direct-caller census (2026-08-20) found exactly two callers of
`FUN_006281f0`: `FUN_00619e30`, the action-label builder, and `FUN_00625840`,
the retained controller-list renderer. There is no third gameplay-HUD caller.
`FUN_00619e30` itself has exactly one direct caller: `FUN_004bd720`, the generic
localized-token expander. It recognises the `0xf000` action-token class, masks
the low action byte, and asks `FUN_00619e30` for the binding label. Therefore
the shipped tutorial tokens `$POWER`, `$GUARD`, `$MOVE`, `$ATTACK`, `$SMASH`,
`$ALLY`, and `$TARGET_LOCK` all reach the existing pad-selection/glyph path.

This corrects the old conclusion that another label caller was missing. The
natural gameplay follow-up on 2026-08-21 closed #87 and #90: the remaining
keyboard wording came from `CPopupDialog::create` replacing eight localized
dialog assets with PC-only `igct.bnx` strings. `dialog_prompts.c` scopes the
shared localization lookup to that exact call and, for the player's active
assigned controller source,
asks the already-loaded dialog parser for its own controller-authored `text`.
Keyboard keeps the PC override and unrelated localization calls retain the
retail body through the JIT.

A windowless run advanced the retail conversations, walked to the switching
terminal and naturally triggered `switching_hint`. The popup contained d-pad
and A glyphs with no `[LEFT CLICK]` or `[???]`; shutdown measured 7,259/7,259
pad labels, zero original names, one controller asset, zero PC overrides and
eight unrelated localization calls. The scoped wrapper's mapped addresses,
parser ABI, return value, stack effect and live controller/no-controller choice
are independently covered by `tests/test_dialog_prompts.c`.

The `FUN_006294b0` override added alongside this note is correct for the
path it covers (a row with a pad binding names it, whatever slot it sits in,
while that assigned Xbox pad is the active source) and costs nothing where it
does not fire. It is
kept because it is the right behaviour and because its zero count is the
measurement above.


## Historical derived-font capture: the glyphs rendered in game

The prompt bar of the main menu, with a pad connected and the retired derived
pack active, drew

    [B] BACK        [A] SELECT

with the port's SVG Xbox button art -- `scratch/shots/ship.png`, zoomed in
`scratch/shots/glyph_zoom.png`. This remains evidence for label selection,
codepoints and art, but it is not a capture of the current native atlas path.

**The cause of the years-long "the game will not draw our codepoint" was a unit
error in the retired font builder.** XMen2.exe's glyph metrics are PIXELS:
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

The removed `make_pad_font.py` was then made to refuse a glyph whose height fell
outside the range the font's own glyphs used, and reported:

    metrics units ok: new glyphs are 18 tall, the font's own run 3..22

### What that historical route measured

* Measured: the menu prompt bar and the conversation prompt, both drawing the
  correct button for the bound action (`Back` shows B, `Select` shows A),
  through the retained `FUN_006281f0` boundary, on the SHIPPING codepoints
  0x80.. -- no printable-range restriction exists.
* Measured: with a pad connected the prompt follows the pad rather than the
  keyboard, because the label-selection override answers with the row's pad
  binding (7,873 of 7,873 label reads in one run).
* Measured: a naturally triggered gameplay `switching_hint` selects pad labels
  7,259 of 7,259 times and uses its localized controller-authored popup text
  once, with zero original key names, zero PC popup overrides and no mouse or
  unknown-key prose (historical controller-only run C231; current policy C237).
* Historical limitation, removed by the native atlas: a prompt on a screen
  whose text used `X2F_big`, `X2F_hud_PC` or `font_XMEN_digital` would have
  remained blank because the old builder refused their pixel format.
* NOT measured: real hardware. Everything here is the synthetic pad.


## Historical corrections after the first derived-font capture

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
The retired builder's `blit` then wrote the source rows reversed, and its
selftest used a cell whose
rows DIFFER so it can see the flip -- the old one used a uniform cell and could
not have.

**2. The glyph sat five pixels above the line.** `baseline` is not per-glyph
geometry: every drawing glyph in this font uses the same value (11 of 166
sampled, 83 agreeing), so it is the ascent -- the distance from the glyph box
top down to the text baseline. Deriving `CELL - 2` from the letters'
height/baseline relationship lifted ours. The retired builder then took the
font's own value, by majority of the glyphs that draw.

**3. The square brackets are wrong around a picture of a button.** `[ENTER]`
reads as a key; `[A]` does not -- no console prompt draws brackets round its
button art. A third override on `FUN_00619e30` removes them, and ONLY when the
composed label is exactly one of this port's glyphs in brackets: a keyboard
name, a one-character keyboard name like the `A` key, and the unmapped `[???]`
all keep theirs. The test covers all four and was checked against two
mutations -- never stripping, and stripping anything short.

The retired delivery's final result was `B BACK` / `A SELECT` with the buttons
upright, on the baseline and unbracketed:
`scratch/shots/final_prompt.png`. The selection, bracket-removal and measured
pixel-unit/baseline findings survive in the current native implementation.
