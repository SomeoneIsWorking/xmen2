# Strategy: run the guest at runtime + native overrides

**Decision: execute the PC build's own machine code at runtime, and replace
functions with hand-written native code incrementally. Target is the PC build,
not the Xbox build.**

This document replaces the retired code-generation plan. The offline lifter
turned Ghidra-derived function boundaries into 116,500 functions and roughly
307 MB of generated source at build time. Commit `27f0a7b` deleted that corpus,
its generator, generated dispatch tables, and the inputs whose only consumer
was the generator.

The replacement is not an engine choice: the gameplay product is native
overrides plus `shared/x86port`'s on-demand JIT for every remaining guest path.
An interpreter may exist only in a separately built test/diagnostic target. It
must not be linked into, selectable by, or reachable as fallback from the
gameplay target. See the canonical contract in
`shared/jit-common/docs/migration.md` and issue I004 there.

Measurements behind this are in `docs/info/claims/` with their falsifiers.

## Why not a clean reimplementation

A clean reimplementation is bounded but *long*: the exe→engine contract is 794
named symbols (C001), yet behind it sit a scene graph, renderer, animation
system and the game's own logic. Nothing runs until a great deal of it exists,
and the first playable build is person-years away.

Executing the shipped binary inverts that. The whole game runs from early on,
and each subsystem is replaced with native code *while the rest keeps working*.
What you buy is **ownership**: a native, buildable, portable codebase where any
function can be traced, intercepted or swapped — none of which Wine gives you,
because Wine runs the original binary rather than yielding a program you own.

An earlier claim (C003) said this was "value-negative" here because both shipped
builds are already x86, so Wine already solves the problem. **That was wrong and
has been falsified.** It scored the port only on escaping an alien ISA and
ignored ownership.

## Why runtime execution rather than generated source

Both answer "what runs the guest's code" and both keep the same override model
above it. The reasons generated source lost are cost, not capability — and note
that none of them argues against translating guest code, only against emitting
it as build input:

- **The build.** 307 MB of generated C across 89 translation units, regenerated
  from the player's own images, is the single biggest thing in the build and it
  had to exist before anything could run. The binary went from ~272 MB to 34 MB
  when it was deleted.
- **Coverage was a permanent job.** x86 has variable-length instructions, so
  code/data separation is undecidable in general; every function the lifter
  could not see was a hole to seed and re-lift. `_initterm`'s constructor
  tables — function pointers in `.rdata` that static analysis never marks as
  code — produced an abort listing every target so it could be fed back as
  seeds. A runtime JIT decodes the bytes actually reached and the whole category
  disappears.
- **Ghidra was in the loop.** A maintainer-only tool can never be a player
  prerequisite (global rule), so a static pipeline meant shipping derived
  artefacts and keeping them in step with the images they came from.
Early interpreter timing was useful only while bootstrapping the execution
boundary. It predates whole-program runtime execution, native override
composition, and representative gameplay, so it is explicitly not product or
performance evidence. In 3D gameplay pure interpretation led to multi-second
frame times. The x86 basic block JIT in `shared/x86port` is integrated into
`src/native/x86_engine.c` with intercept support for host thunks, native
overrides, and setjmp frames. It is the only permitted gameplay engine.

Its first landing (`775712c`) rendered black and was never verified; issue #140
fixed four causes (blocks translated through interception points, a JITted
thread monopolising the guest lock, a thread-shared engine call stack, and —
root-caused 2026-09-03 — `jit_intercept` gating the native-override hand-back on
an engine frame that goes NULL once boot nesting passes `ENGINE_FRAMES_MAX`,
which silently ran the font, splash, prompt-glyph and native-FMV overrides as
raw guest x86 and made the intro decode through the guest's own MMX kernel).
It now reaches the rendered main menu and plays the intro FMV through native
FFmpeg. Later in-game differential runs exercised hundreds of millions of JIT
block entries with native overrides active. Those observations establish real
execution, not the representative-gameplay, product-link, or host-backend gates
listed below.

## Why the PC build, not the Xbox build

| | PC build | Xbox build |
|---|---|---|
| code | 5.58 MB (exe + 16 DLLs) | 4.05 MB `.text`, one static image |
| named entry points | 50,581 (export tables) | **0** (no export table; kernel imports by ordinal) |
| GPU boundary | D3D8 COM API — documented, dxvk-d3d8 exists as reference | NV2A push buffers — this is the xemu problem |
| oracle | already working (C005) | would have to be stood up |

The GPU boundary alone would decide it: replacing D3D8 API calls is tractable
work, whereas the XBE statically links D3D/DSOUND/XGRPH/DOLBY and drives the
hardware directly. This is unchanged by the execution decision above.

## Instruction mix, measured

`InstrHisto.py` counted instructions inside Ghidra-identified function bodies —
*not* a linear sweep, which decodes padding and data as code (objdump reported
29% `nop` and 146 `aas` for libIGDisplay, both artefacts).

| | XMen2.exe | libIGDisplay.dll |
|---|---|---|
| functions identified | 11,106 | 521 |
| instructions | 643,647 | 8,754 |
| distinct mnemonics | 186 | 54 |
| top-50 mnemonics cover | 98.76% | 99.95% |

The integer core (MOV/PUSH/CALL/LEA/POP/TEST/ADD/JZ/CMP/JNZ) is 80% of the exe,
x87 is 41 mnemonics at 5.83%, SSE is 0.18%.

**What this measured, and what it no longer decides.** It was collected to argue
that static translation could reach enough of the binary; the *coverage* half of
that argument (77.5% of exec bytes inside identified functions) is moot, because
runtime translation does not need precomputed boundaries. The *mix* half still
holds and still sizes work: it is the opcode surface `x86port` has to implement,
and it is why 3DNow!'s 20 opcodes were the first module (they clear 90% of the corpus's
holes, I004 §1). Instruction-count coverage is not semantic coverage in either
design: one mis-modelled flag or x87 precision case breaks execution regardless
of histogram share.

## Shape of the work

- **The product engine** (`shared/x86port` JIT): decodes and dynamically
  translates the guest's own bytes, read from the player's images at run time.
  Zydis is decode-only; semantics and host-code emission belong to x86port.
  There is no gameplay engine selector.
- **The loader** (`src/native/pe_map.c`, `guest_modules.c`): maps each image,
  applies base relocations when it has to relocate, and binds the IAT.
- **The host surface** (`src/native/host_imports*.c`, and the modules behind
  it): what this port implements of Win32, the CRT, D3D8, DirectInput and
  DirectSound — 398 import entries across 12 DLL surfaces, plus the run-time
  export registry for what `LoadLibrary`/`GetProcAddress` resolve. A slot
  nothing can answer is poisoned and faults BY NAME rather than binding to
  something plausible.
- **Overrides**: hand-written native code replacing guest functions one at a
  time, registered at a module + linked entry point. `x86_guest_body()` runs
  the original through the JIT when an override wants to super-call it.
- **Test oracle**: the interpreter is linked only into a separately built test
  target used for focused differential diagnosis. It is absent from product
  configuration, selection, fallback, and linkage.
- **Shared Alchemy foundations (partial)**: `shared/alchemy` already owns
  native format/render-data and input libraries plus XMLB/ARK tools used by
  X-Men 2 tooling. No library, header, or call from it reaches `x2native` today;
  current gameplay behavior remains retained guest code or title-local native
  ownership. X-Men 2 must create and prove the first consumer seam.
- **Harness**: the port diffed against the Wine oracle, plus the port's own
  control channel for driving a running build.

### Order

There is no lifting order any more — every module is executed through runtime
translation from the first frame. Before further native-renderer work is
treated as a landed product milestone, complete these architecture gates:

1. Split the interpreter into a separately built test/diagnostic target and
   remove it from `x2native`'s link closure, CVar/environment/CLI vocabulary,
   and every fallback path.
2. Add a product audit that proves the gameplay target contains the JIT and no
   interpreter or static guest substrate, then exercise both a native override
   and its scoped original-body JIT call.
3. Publish the consumer-proven revision already present in the canonical
   `shared/x86port` checkout, reconcile this project to that immutable remote
   revision, and keep title-local CPU semantics out of this repository.
4. Run a bounded representative interactive gameplay scenario with native
   overrides active, nonzero JIT blocks, and independent CPU/memory/timing/
   device evidence. Boot, menu, FMV, and unattended loops are checkpoints only.
5. Qualify each declared host backend. x86-64 exists; Apple Silicon and Android
   ARM64 require a real JIT backend and may not fall back to interpretation.
6. Integrate `alchemy_input` first through a narrow guest
   `igControllerManager` adapter, A/B-verified against the retained DirectInput
   path as specified by `shared/alchemy/docs/input.md`. Extend further shared
   engine behavior only after X-Men 2 shipping-path evidence proves the
   contract title-neutral. Do not start MUA migration until every X-Men 2 goal
   is verified; then migrate MUA without rewriting its gameplay.

Native override work then continues at the D3D8 seam described below.

## What carries over

- Runtime PE image identity, sections, imports, exports, and relocations remain
  necessary loader and RE facts. They do not become a precomputed function map.
- The unmodified retail PC build under Wine remains an independent behavioral
  oracle (C005). It is not a generated or replacement-DLL product mode.
- Boundary observations already recorded from the retail binary remain valid
  evidence when their falsifiers still hold. No static generated product is
  rebuilt or retained as an oracle.
- `docs/RE/ark.md` — the ARK meta-object system (C008, C009). The guest's own
  code reproduces it automatically; overrides need it understood.
- Xbox-derived controller and behavioral observations remain evidence. The
  obsolete Xbox static build/run path is not a second product and is removed
  after those facts are consolidated into their living authorities.

## Known limits carried forward

- Frame-level A/B is not deterministic yet (boot-movie timing varies run to run).
  Only module-load sets and error logs are currently comparable.
- The harness needs the Lutris prefix (`WINE_PREFIX` in `.env`) for DXVK's d3d8;
  this Wine build ships no builtin d3d8.
- The product-link/selector audit, representative interactive gameplay case,
  canonical x86port publication/pin reconciliation, and ARM64 backend
  qualification are still open; see `docs/project-state.md` S001, S002, S010,
  S014, and S018.

## Forbidden retired paths

Do not regenerate, compile, link, package, or run an offline-translated guest
corpus for either the PC or Xbox build. Do not use a static dispatcher,
generated function seeds, replacement guest-code DLL, or precompiled title
substrate as a product or permanent oracle. Runtime JIT code generation is the
shipping mechanism and is not part of this prohibition.

## Removing the D3D8 seam

**Decision, 2026-08-26 (the user's): the D3D8 layer is a way station, not a
destination. The port owns the engine's renderer instead, and `src/d3d8/` is
deleted from the top down as that happens.**

The reason is the same one that chose running the guest ourselves over Wine:
*ownership*. A D3D8
emulator is the one part of this tree that is not ownership — it is a
compatibility shim reconstructing intent that the engine had a moment earlier
and threw away. The retired prompt-glyph plan demonstrated the cost: it would
have encoded an atlas index in a UV float and split a lowered D3D8 draw. The
native replacement instead brackets libIGGfx's semantic text draw, keeps the
engine's own finalized transform, and submits the SVG before lowering. That is
the ownership boundary this strategy means (`RE/text.md`).

### What the seam actually carries, measured

400 frames of `act0/tutorial/tutorial1`, `--no-window --d3d8 --run`
(C270, `scratch/logs/seam-census.txt`):

| | |
|---|---|
| draws | 36,354 (1,604 clears, 400 presents) |
| render states set by the engine | 47 — **13 read by the draw path, 34 not** |
| transforms set | 14 — 3 read (WORLD/VIEW/PROJECTION), 11 not |
| vertex formats | 7 distinct FVFs; 1 vertex shader (VS 1.1, 772 draws) |
| resources | 101 textures, 434 vertex buffers, 383 index buffers |
| state blocks | 784 created, 784 applied, 0 re-captured |

So the engine speaks a small fixed dialect, and a third of what it sets this
backend already ignores. `src/gpu/` beneath it is already guest-free by design
("this knows NOTHING about the guest" — `gpu_draw.h`), which is what makes the
seam removable rather than load-bearing.

### How it comes out — top down, never by decree

`libIGGfx` is **5,580 guest functions**. The seam cannot be removed by
deciding to; it is removed when its last caller is ours, and until then it is
the only working path. So: port a renderer subsystem natively, watch its D3D8
call sites go to zero, and delete what is then unreachable. Nothing is deleted
speculatively.

### First native slice: SVG prompts

The first 2D/UI slice now crosses above D3D8. The exe's `FUN_005ee400`
supplies the engine-laid-out prompt rectangles. The port brackets
`Gap::Gfx::igDxVisualContext::drawNonIndexed` at libIGGfx `0x100352d0`,
super-calls its nested `updateContextState` at `0x10034e60`, takes the
engine-converted world/view/projection from `computeMatrix_Dx` at `0x1003ec10`,
and submits its own RGBA SVG atlas before the stock ASCII. The port retains a
collapsed retail emitter call for each native glyph so even a one-glyph
controller label reaches Alchemy's own finalizer. Windowless, silent,
unbounded runs verify both shapes: 1,188 keycap quads in 99 batches around
stock `ENTER`, and 1,073 pure controller icons through 1,073 matching nested
finalizers with zero refusal, desync, or orphan counts. The latter capture
shows the native A icon beside `CONTINUE...`.

This proves the direction, not completion of the 2D renderer. The prompt path
still deliberately consumes Alchemy's layout and transform, while stock ASCII,
panels, sprites, batching and the other libIGGfx UI draws continue through the
guest engine and the D3D8 host. **The broader 2D/UI path remains the first
subsystem target** because it is screen-space, unlit and unshaded. Each
semantic owner moves natively, its old D3D8 call sites are measured to zero,
and only then is that part of the compatibility layer deleted.

### The prior attempt, and why its verdict does not settle this

`src/vulkan/` substituted the engine's ARK renderer classes so the engine never
called `Direct3DCreate8`. It was superseded by `src/d3d8/` (C129) and the
codemap still records C128's decisive-sounding argument against it — that
inherited engine helpers below the vtable reach the device, so a cut above the
device can never be complete.

**That argument was withdrawn by its own author.** C128's re-confirmation says
so: the `setupTextureStages` NULL dereference was reached only because
`vk_open` called `setupAll` with no device, "a fault of my own making, not an
architectural wall". The case against owning the renderer is therefore weaker
than this tree currently reads, and C129's cut is a good *staging* decision
rather than a permanent one.
