# The exe's own text pipeline: fonts, measurement, and the glyph drawer

Established 2026-08-26 by static reading of the recompiled bodies, the PE's
`.rdata` vtables, a Ghidra headless decompile of the cluster, and one live
400-frame tutorial run with the reached set armed over `0x596000-0x5a0000`
(`scratch/logs/reached-text-err2.log`) plus an `X2_TEXTURE_PROBE` run
(`scratch/logs/probe-err.log`). Everything below is measured against the
retail `XMen2.exe` at its linked addresses.

**Why this page exists.** The port wants controller/keyboard prompt art drawn
as its own SVG-derived glyphs WITHOUT touching any font asset or in-memory
font record (the derived `X2_ASSETS` pack was stage one and is asset editing;
an in-memory atlas-compositing attempt was abandoned before drawing anything).
Doing that at the RENDERER requires knowing exactly where strings become
glyph quads. That path had no static call sites anywhere -- every hop goes
through object vtables -- so it took a runtime caller probe to pin down.

## The font table

* A lazily-initialised singleton object lives at **`0x008161f8`**.
  `FUN_00597c20` ("getTable") initialises it once -- four `FUN_00596a70`
  font loads, records `0x1c18` bytes apart starting at `+4`... the object's
  first dword is the **vtable `0x69d69c`** -- then returns `0x008161f8`.
  It was entered 10,936 times in the reference run.
* Font loading itself is `FUN_005980f0` (vtable `+0x00`): parses
  `ui/fonts/fonts_pc.xmlb` / `fonts_HD.xmlb` via `FUN_00596af0` into
  `0x1c18`-byte records (header `0x18`, then 256 glyph entries of `0x1c`).
* Vtable `0x69d69c`, by offset:

| slot | target       | role (all confirmed from the bodies)                    |
|------|--------------|---------------------------------------------------------|
| +0x00| `0x005980f0` | loadFonts                                               |
| +0x18| `0x00597200` | getRecord(index) -- x3211/run                            |
| +0x1c| `0x005972a0` | getTexture(index) -> texture object -- x4931/run         |
| +0x34| `0x00596df0` | setColor-ish state helper -- x538/run                    |
| +0x38| `0x00597c90` | measureExtent(string, ...) -> float in ST0 -- x645/run   |
| +0x3c| `0x005972d0` | markup tokenizer -- x212/run                             |

* Glyph entry (`record + wchar*0x1c`), named by the loader's own attribute
  strings: `+0x00` width i16, `+0x02` height i16, `+0x04` horizAdvance i16,
  `+0x06` horizOffset i16, `+0x08` baseline i16(+dword read), `+0x0c..+0x1b`
  atlas UVs `s t s2 t2` as f32.
* The measurer reads ONLY bytes (narrow walk); the drawer walks a WIDE
  string and gates on `(wchar & 0xff) == wchar`, so both representations
  agree that glyphs live in 0..255.

## Measurement: FUN_00597c90

`__thiscall(this = table, string, ...)` plus stack args including the wrap
width and the font index. Per character it walks a jump table at `0x597fa8`
for chars `0x0a..0x7e`: newlines, `$`-markup, tab stops (tab advance =
`word[record+0x384] << 1`), color tokens; ordinary characters add
`word[glyph*0x1c+4]` scaled. It accumulates pen X and wrapped Y and returns
the height in ST0. It has ZERO direct callers -- reached only through
vtable `+0x38` (e.g. from `FUN_0059c760`).

## The draw call pair (where strings become quads)

Found by overriding `getTexture` and recording return addresses
(`src/native/text_caller_probe.c`, `X2_TEXTURE_PROBE=1`): 2,439 calls,
eight distinct return sites, two of which are 1,207 calls each --
`0x005ee63d` and `0x005ee663`, both inside `FUN_005ee780`.

* **`FUN_005ee620(fontIndex)` -- bind.** Stores the index, then
  `getTable -> vtable+0x1c getTexture` TWICE, queries the returned texture
  object's width/height through ITS vtable slots `+4`/`+8`, and caches
  scales derived from them into the draw object (`+4..+0x28`). This is why
  getTexture fires per string batch, not per frame.
* **`FUN_005ee780` -- the per-character loop** (`__regparm3`; ECX/EDX carry
  hidden args). Walks a wide string pointer kept at `[drawobj+0x10]`:
  * `wchar < 256`: glyph record at `wchar*0x1c + [drawobj+0x18]`;
    reads width/height/advance/baseline shorts and the UV floats
    (`psVar19+6/+8/+10/+0xc` = s/t/s2/t2), v-flips them against
    `_DAT_0068012c` (= 1.0), positions the quad using cached scales
    `param_9[7]/[8]` and offsets `param_9[9]/[10]`, submits it through
    **`FUN_005ee400`** (the quad emitter into the current batch), then
    advances the pen by the record's advance.
  * `wchar == 9` (tab) advances twice the record-at-offset-900 field;
    `wchar == ' '` skips drawing but still advances.
  * `1000 <= wchar < 2000`: colour pop/push against a 4-deep stack at
    `param_3+0x21410` (count at `+0x21424`).
  * `wchar >= 3000`: absolute pen set (`wchar - 3000`).

`FUN_005ee400` transforms and pushes the quad under whatever texture the
engine has bound for this text -- the binding established by `FUN_005ee620`.

## What this makes possible

The port can now put its prompt art on screen entirely at the renderer:

1. Override **`FUN_005ee780`**. If the wide string contains none of the
   port's private codepoints (`X2_PAD_GLYPH_FIRST..X2_KEYCAP_GLYPH_LAST`),
   super-call and change nothing -- that is every ordinary string, verified
   cheaply before anything else runs.
2. For a string that DOES contain one: split around each such codepoint,
   NUL-terminate the segment in place, super-call per segment (stock glyphs,
   stock markup, stock pen updates), and between segments emit our own quads
   through the same **`FUN_005ee400`** under our OWN texture binding, sized
   by the same cached scale fields the stock quads use.
3. Mirror the same split inside an override of the measurer
   (**`FUN_00597c90`**) so layout gives our codepoints real widths instead of
   the zero the untouched font record answers with. No font record is
   written; the answer comes from the port, at the renderer.

The pixels come from the shared `port-assets` SVG sets rasterised at build
time into a generated, port-owned atlas (same pattern as
`src/recomp/gen/font_tier_ratio.h`) -- never patched into a copy of any game
font, on disk or in memory.

## Stage one ran, and the answer was NO (C267)

The plan above is a plan, not a measurement. Its first step assumes the
port's labels arrive at `FUN_005ee780` as wide strings carrying
`0x80..0x93`. They do not -- at least not in the scenario measured.

`src/native/prompt_glyph_draw.c` sits on the glyph loop and classifies every
string. On a boot-direct tutorial run
(`X2_BOOT_MAP=act0/tutorial/tutorial1`, `X2_MAX_FRAMES=1200`,
`--no-window --d3d8 --run`):

| | pack ON | pack OFF |
|---|---|---|
| strings at the glyph loop | 4541 | 4557 |
| carrying `0x80..0x93` | **0** | 0 |
| carrying some other non-ASCII wchar | 2273 | 2281 |
| labels composed by `x2_override_00619e30` | 2264 keycap | 0 (2272 unchanged) |

Enabling the pack changes NOTHING that reaches the glyph loop. The A/B is
the point: the ~2,270 non-ASCII strings look like they might be the labels
until the pack-off column shows the same count and the same words
(`9d28 01f2 08e2` -- the engine's own above-256 control values, which the
loop routes to its colour and pen-set branches, not to a glyph).

The zero is a measurement rather than silence because the detector has been
made to answer one: `ctest -R prompt_glyph_draw` drives
`x2_override_005ee780` itself over guest memory with a composed keycap
label and watches it counted, alongside the boundaries either side of the
range and the exact above-256 words seen on the real run.

**The mechanism this points at.** `prompt_labels.c` rewrites the retail
label IN PLACE as a NARROW byte string (`WR8`, `strlen`, codepoints
0x80..0x93 as single bytes). `FUN_005ee780` walks a WIDE string. Something
between the two widens, and `0x80..0x9f` is exactly the byte range a
multibyte conversion treats as lead bytes -- which would fold our codepoint
and the character after it into one above-256 wchar. That is a hypothesis,
NOT measured: the pack-off column shows the same above-256 words with no
port label anywhere, so those particular words are the engine's own.

**What is still open.** This run does not establish that the label
elements were ever meant to be ON SCREEN. 2,272 label reads happen either
way, and a read is not a draw. The decisive scenario is the Controls
binding menu, where keycap labels are unquestionably drawn; until that runs,
"not drawn in this scenario" and "drawn by another path" are not separated.
Step 1 of the plan below must not be built on until it is.

## Facts that killed the earlier plans, recorded so they stay dead

* The derived font pack (`tools/make_pad_font.py` + `X2_ASSETS`) WORKS
  (C219, issues #87/#91) but is an asset edit standing in for a port feature:
  per-tier, per-localisation work inside a copy of shipped data. Replaced by
  the renderer seam above; kept runnable until the replacement draws.
* In-memory atlas compositing (`prompt_glyphs_runtime.c`, removed today)
  hooked the font loader to find the atlas igImage behind the font record.
  Besides being font-mediated, its stage-one lookup never found anything:
  on the real run, ALL FOUR loaded HD fonts reported NO texture object at
  `record + 0x1c18` (`UI TEXT/PROMPT GLYPHS` lines,
  `scratch/logs/reached-text-err.log`). Whatever attaches the atlas does so
  after the loader returns -- the hook point was wrong even on its own terms.
