# The exe's own text pipeline: fonts, measurement, and the glyph drawer

Established 2026-08-26 by static reading of the recompiled bodies, the PE's
`.rdata` vtables, a Ghidra headless decompile of the cluster, and one live
400-frame tutorial run with the reached set armed over `0x596000-0x5a0000`
(`scratch/logs/reached-text-err2.log`) plus an `X2_TEXTURE_PROBE` run
(`scratch/logs/probe-err.log`). Everything below is measured against the
retail `XMen2.exe` at its linked addresses.

**Why this page exists.** The port draws controller/keyboard prompt art as its
own SVG-derived pixels without modifying or copying a shipped font asset. The
retail text engine still owns layout: the port publishes metrics and the
font's own evidenced baseline into otherwise-empty private codepoint records,
but never writes atlas UVs or pixels there. The generated RGBA atlas and its
GPU texture belong entirely to the port. Finding that split required locating
both where strings become glyph quads and where libIGGfx submits the semantic
text batch; neither path has useful static call sites because the hops are
vtable-mediated.

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

## The ownership split that shipped

The retail engine measures and positions the string; the port owns the new
pixels and submits them before the stock ASCII in the same semantic text draw:

1. `prompt_glyph_metrics.c` publishes width, height, advance, offset and the
   font's modal scaled baseline for the port's private codepoints. It writes no
   UVs, so the game font never becomes the pixel owner.
2. `prompt_glyph_draw.c` follows the stock wide-string loop and intercepts
   each private-codepoint call to **`FUN_005ee400`**. Before the loop begins it
   validates the batch colour, every codepoint and capacity for the whole
   string. It then keeps the engine's text-plane rectangle and batch colour,
   substitutes the matching port-atlas UVs, queues the quad, and super-calls
   the retail emitter with `x1=x0` and `y1=y0`. That collapsed call draws no
   stock-font pixels but preserves the engine's vertex and finalizer contract,
   including for a string containing only one controller glyph.
3. `prompt_glyph_batch.c` brackets
   `Gap::Gfx::igDxVisualContext::drawNonIndexed` at libIGGfx `0x100352d0`.
   Its nested `updateContextState` override at `0x10034e60` super-calls first,
   then submits the queued SVG quads with the engine's finalized transform.
   Control returns to the stock draw, so ordinary text lands over the keycap
   background in the intended order.
4. `gpu_prompt_glyphs.c` owns the retained RGBA texture and vertex buffer and
   submits a `GpuDraw` from the queued rectangles. The pixels come from the
   shared `port-assets` SVG sets rasterised at build time into the generated,
   port-owned atlas (`tools/render_prompt_glyphs.py` ->
   `src/recomp/gen/prompt_glyph_atlas.h`). No shipped font contributes pixels
   to it.

## Stage one ran: the labels DO arrive (C268, after C267 was falsified)

`src/native/prompt_glyph_draw.c` sits on the glyph loop and classifies every
string. On a boot-direct tutorial run (`X2_BOOT_MAP=act0/tutorial/tutorial1`,
`X2_MAX_FRAMES=1200`, `--no-window --d3d8 --run`, `X2_PROMPT_GLYPHS=1`):

* 4,581 strings reached `FUN_005ee780`
* **1,142 of them carried 13,704 prompt codepoints**, in exactly the shape
  `prompt_labels.c` composes:

      0090 0091 0091 0091 0091 0091 0092 0092 0092 0092 0092 0045 006e 0074 0065 0072 0093
      KEYCAP_LEFT  MIDDLE x5            REWIND x5             "Enter"      KEYCAP_RIGHT

So **step 1 of the plan above is sound**: an override on `FUN_005ee780` can
test each wide string for the port's codepoints and super-call unchanged for
every ordinary one.

### The full chain, measured

| hop | what happens |
|---|---|
| `x2_override_00619e30` | composes the label as NARROW bytes (`WR8`, `strlen`) |
| `FUN_004bd720` | token resolver: calls the label builder with the action id in the low byte, then RETURNS that string pointer to its own caller at `L_004bd7ff` |
| `FUN_00596df0` (`0x00596f5a`) and `FUN_005ef2e0` (`0x005ef757`) | the two consumers, x1143 and x1142 in one run |
| `FUN_005ef2e0` | markup -> wide line buffers. The widening is a plain `MOVZX AX,BL` at `0x005ef7b3`, so a 0x90 byte becomes wchar 0x0090 unchanged |
| `FUN_005ee780` | walks that wide buffer |

The token-resolver census lives in `prompt_labels.c` and reports with its
denominators (`FUN_004bd720` ran 5,448 times, handed OUR buffer back 2,285
times). Its 1,142 consumptions at `0x005ef757` equal the 1,142 strings the
glyph-loop detector sees -- two independent instruments agreeing.

### The argument binding, and the instrument that lied about it (I069)

**`FUN_005ee780` takes the wide string as its FIRST STACK ARGUMENT**, at
`entry_esp + 4`. Read out of the retail body: the character walk at
`0x005ee7dc` is `MOV EAX,[ESP+0x40]` / `MOVZX EAX,word [EAX]`, and the
prologue's `SUB ESP,0x2c` plus four pushes (EBX, EBP, ESI, EDI) puts ESP
0x3c below entry, so `[ESP+0x40]` is `entry_esp+4`. The entry prologue reads
the same slot as `[ESP+0x30]` before the pushes, which agrees.

It is NOT EDX. `0x005ee797` overwrites EDX from `[EDI+0x8]` before EDX is
ever read, so EDX is not an input at all.

This detector first shipped reading `C->edx`, on the strength of a
"__fastcall(ECX=owner, EDX=&wide buf)" note. It reported 0 prompt codepoints
over 4,541 strings and produced claim C267, "the port's labels never reach
the glyph loop" -- which was false. Whatever the caller left in EDX often
pointed at real wide text, so the wrong pointer decoded as `"Cyclops"` and
the legal screen and read like a working instrument. A pack-on/pack-off A/B
appeared to confirm the zero and confirmed nothing: neither column was
looking at memory the feature touches.

The unit test did not catch it because the test set `cpu.edx` ITSELF -- it
validated the classifier against an argument binding it had assumed. It now
builds a real guest stack (return address at ESP, string pointer at ESP+4)
and calls the override the way the engine does, so a wrong binding fails.

**The rule this earns:** on a recompiled body, read the argument out of the
retail prologue, never from a register a comment claims. Wide text is common
enough in neighbouring registers that a wrong pointer produces plausible
strings rather than obvious garbage.

Two hypotheses recorded alongside C267 are dead with it: the labels were
never "not drawn in this scenario", and the narrow->wide step performs no
multibyte lead-byte folding -- it is a plain zero-extend.

### The two signatures the native path uses, measured from a live call

Both were dumped from a real draw of a composed label rather than derived
from frame arithmetic -- ESP moves across the body's own calls, and hand
arithmetic on those slots is exactly what produced the EDX mistake above.
The retired `X2_GLYPH_ARGS=1` probe scoped that dump to a prompt-carrying
string. It was deleted after the ABI was encoded in the shipping interception.

**`FUN_005ee780(...)` -- `RET 0x1c`, seven stack args, ECX = owner**

| arg | slot | measured value | meaning |
|---|---|---|---|
| 1 | `entry+0x04` | `0x700ff388` -> `0090 0091x5 0092x5 "Enter" 0093` | **the wide string** |
| 2 | `entry+0x08` | `0x700ff348` | the batch/element object -- also what `FUN_005ee400` takes in ECX |
| 3 | `entry+0x0c` | `298` | **pen x start** |
| 4 | `entry+0x10` | `658` | pen y |
| 5 | `entry+0x14` | `0x008b6b38` -> `[0]=0x0292012a` (= args 4,3 packed), `[2]=1.0f` | the draw/style object |
| 6 | `entry+0x18` | `0x700ff33c` -> `[1]=ECX` | pen base; the pen starts at `*(arg6) + arg3` |
| 7 | `entry+0x1c` | `0x700ff35c` -> `[1]=512.0 [2]=256.0 [3]=1/512 [4]=1/256` | atlas dims and UV scales cached by `FUN_005ee620` |

The pen is a LOCAL (EBP), advanced per character by the glyph record and
**never written back**. Segmenting therefore means re-invoking with an
adjusted `arg3`, not relying on carried state.

**`FUN_005ee400(x0, y0, x1, y1, u0, v0, u1, v1)` -- `RET 0x20`, ECX = the
batch (arg2 above).** It applies no arithmetic of its own: it pushes four
vertices through `[ECX] -> vtable +0xc`, one per corner, with `[ECX+8]`
riding along as the vertex state. So a quad emitted through it lands in the
same space as the engine's own text.

### What untouched private codepoints did before native metrics

Drawing `0090 0091x5 0092x5 "Enter" 0093` emits, in order:

* **11 degenerate quads** -- `x0==x1`, `y0==y1`, all at pen x `19.022`, UVs
  `(-0.001, 0.998, -0.001, 0.998)`. That is every one of our codepoints:
  the untouched font record for `0x90..0x93` has no size and no advance, so
  they draw nothing and move the pen nowhere.
* **5 real quads** for `"Enter"`, the pen advancing `18.489 -> 25.067 ->
  33.600 -> 39.111 -> 45.689`.
* a final degenerate quad at `53.333`, the closing `0093` after `"Enter"`
  moved the pen.

This is the measured hole the native metrics fill. The label now composes as
designed: left cap at 14.933, five middles advancing to 31.822, five rewinds
stepping back to 20.622, `"Enter"` over the top from 16.356 to 43.556, and the
right cap at 51.200. The engine remains the layout authority because the run
is right-anchored: truncating it from 17 to 13 to 12 wchars moved the first
quad from 19.022 to 39.289 to 46.756. A +100 perturbation of arg3 shifted every
quad by exactly `100 * 0.177778`, settling the pen control by experiment.

## Native SVG submission at the semantic text boundary

The vertex sink behind `FUN_005ee400` is `FUN_005840a0`, reached through the
batch's `[ECX] -> vtable 0x0069c904 +0xc`. It stores text corners as `(x,0,y)`;
the owning batch's world/view/projection places that plane on screen. A live
callsite probe separated the draw that matters from an adjacent indexed draw:
the stock text plane returns inside libIGGfx `drawNonIndexed` at `0x10035489`,
whose owning body begins at **`0x100352d0`**.

`prompt_glyph_batch.c` overrides that outer body only to bracket its lifetime.
When its nested `updateContextState` at **`0x10034e60`** runs, the override
super-calls the engine body first. `ui_transform.c` independently mirrors the
converted projection, world and view outputs of `computeMatrix_Dx` at
**`0x1003ec10`**, then publishes the complete engine-owned MVP. The port
submits the queued SVG rectangles in the stock `(x,0,y)` text plane at that
point, before control returns to `drawNonIndexed` and the stock ASCII is
submitted. No matrix is reconstructed from lowered D3D state.

The port-owned atlas is RGBA, retained and uploaded by
`gpu_prompt_glyphs.c`. Each harvested rectangle becomes two triangles with
the engine batch colour and the generated atlas UVs. Failure to read the
batch colour or reserve a complete string keeps that entire string on the
retail path. Failure to obtain all three engine matrices or submit through the
GPU path discards the matching batch at that exact boundary. The outer batch
wrapper also discards any queue left after a draw returns without its nested
finalizer, so native art cannot leak into an unrelated draw. None of these
conditions is silently accepted.

### Live verification and the baseline defect

The windowless, silent, unbounded run in
`scratch/logs/svg-final-unbounded.log` submitted **1,188 native keycap SVG
quads across 99 semantic text batches**, with zero GPU refusals,
incomplete-transform refusals, queue/colour refusals or glyph/quad desyncs.
`scratch/screenshots/svg-final-unbounded.png` shows `ENTER` aligned inside the
native keycap.

That keycap still includes stock ASCII, so it did not exercise the dangerous
one-glyph shape. Issue #121 records the review finding: bypassing the only
`FUN_005ee400` call could leave `drawNonIndexed` with zero primitives, skip its
nested `updateContextState`, and carry the queued icon into a later draw. The
corrected windowless, silent, unbounded controller run in
`scratch/logs/svg-pad-final-unbounded.log` drove a synthetic Xbox A press and
reported **1,073 one-codepoint controller strings, 1,073 intercepted emitter
calls, 1,073 matching nested finalizers and 1,073 GPU submissions**. It had
zero desyncs, unavailable-codepoint fallbacks, colour/queue failures,
unreadable matrices, cross-context requests, transform/GPU refusals, or quads
left after an unfinalized draw. The headless capture
`scratch/screenshots/svg-pad-unbounded.png` shows the native green A icon
aligned beside `CONTINUE...`.

Together these are the first native 2D/UI renderer slice: layout and transform
still come from Alchemy, but the prompt pixels, texture, vertices and GPU
submission do not pass through the D3D8 emulation layer.

The first native picture placed the keycap one full ascent too low. The cause
was not the matrix or a missing positional offset: `GL_BASELINE` (`glyph+0x08`)
was left zero when the port published width, height and advance. The retail
font's scaled modal baseline is **42** in the verified run. The runtime now
copies that evidenced value into each otherwise-empty private record and
refuses a font with no non-zero modal baseline. It is font-owned layout data,
not a positional constant guessed for this screenshot.

## Withdrawn text-space probe

**"The port cannot draw the result itself" was not established.**
`fa2ace8` recorded that the engine's text space never reaches D3D8 -- 10,815
draws scanned while a prompt was harvested, 366,392 vertices compared, 0
unreadable, no vertex within 57 units of a known text corner -- and concluded
the port must smuggle its quads through the engine's emitter with a sentinel
UV and split the draw in the D3D8 layer.

That negative is not trustworthy and the conclusion is withdrawn. Two defects,
both readable in the deleted probe
(`git show fa2ace8 -- src/d3d8/d3d8_drawcall.c`):

1. **It capped the interesting case.** `n = req->num_vertices > 256u ? 256u :
   req->num_vertices` truncated every draw at 256 vertices, silently and with
   no counter. The UI batch is the LARGEST draw in the frame -- measured on
   frame 350, draw 92 of 94 is a single tristrip of 424 primitives on texture
   71 -- so the probe cut off exactly the draw that could have matched. This
   repo's own rule is to cap the boring case and never the interesting one.
2. **It applied no transform.** It compared raw stream vertices against
   text-space corners, while every draw carries its own `mvp` (frame 350's UI
   batch: x scale 0.00292969, y scale 0.00520833 -- an orthographic UI
   projection). If `FUN_005840a0` leaves coordinates in text space and the mvp
   does the work, a raw comparison could not have seen it either way.

The probe could not have produced a positive, which makes its negative worth
nothing. Recorded as I070, distrusted. The native path above answered the
actual ownership question at a better boundary: it captures the engine's
converted matrices before D3D8 lowering and renders the same text plane.

The sentinel-UV plan built on that negative is dropped for a second and larger
reason: it is a bandaid, and it lives entirely inside the layer this port is
now committed to deleting (`../strategy.md`, "Removing the D3D8 seam").
Encoding an atlas index in a UV float to smuggle it through D3D8 is not a fix.
The verified native route never encodes such a sentinel and never splits a
lowered D3D8 draw.

### What remains

Prompt SVGs prove one native 2D/UI slice, not the whole 2D renderer. Stock
ASCII, panels, sprites, batching, render-state policy and the other libIGGfx UI
draws still use the recompiled engine and D3D8 host. The broader work is to
move those semantic 2D owners across the same native boundary, verify their
output, and delete each D3D8 path only when its final caller is gone. See
`../strategy.md`, "Removing the D3D8 seam".

## Facts that killed the earlier plans, recorded so they stay dead

* The derived font pack (`tools/make_pad_font.py` + `X2_ASSETS`) WORKS
  (C219, issues #87/#91) but is an asset edit standing in for a port feature:
  per-tier, per-localisation work inside a copy of shipped data. The native
  renderer path above now draws the replacement directly from the SVG atlas.
* In-memory atlas compositing (`prompt_glyphs_runtime.c`, removed today)
  hooked the font loader to find the atlas igImage behind the font record.
  Besides being font-mediated, its stage-one lookup never found anything:
  on the real run, ALL FOUR loaded HD fonts reported NO texture object at
  `record + 0x1c18` (`UI TEXT/PROMPT GLYPHS` lines,
  `scratch/logs/reached-text-err.log`). Whatever attaches the atlas does so
  after the loader returns -- the hook point was wrong even on its own terms.
