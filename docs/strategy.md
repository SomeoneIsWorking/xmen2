# Strategy: static recompilation + native overrides

**Decision: recompile the PC build to native C, then replace functions with
hand-written native code incrementally. Target is the PC build, not the Xbox
build.**

Measurements behind this are in `docs/info/claims/` with their falsifiers.
Reproduce them with `tools/pe.py`, `tools/ghidra_scripts/InstrHisto.py`, and
`tools/run_shim.sh`.

## Why recomp rather than clean reimplementation

A clean reimplementation is bounded but *long*: the exe→engine contract is 794
named symbols (C001), yet behind it sit a scene graph, renderer, animation
system and the game's own logic. Nothing runs until a great deal of it exists,
and the first playable build is person-years away.

Recompilation inverts that. The whole binary is translated mechanically, so the
game runs from early on, and each subsystem is replaced with native code *while
the rest keeps working*. What you buy is **ownership**: a native, buildable,
portable codebase where any function can be single-stepped, edited, or swapped —
none of which Wine gives you, because Wine runs the original binary rather than
yielding source.

An earlier claim (C003) said recomp was "value-negative" here because both
shipped builds are already x86 so Wine already solves the problem. **That was
wrong and has been falsified.** It scored recomp only on escaping an alien ISA
and ignored ownership. What survives from it is the *difficulty* argument, which
is real: x86 has variable-length instructions, so code/data separation is
undecidable in general, and this engine dispatches through C++ vtables
throughout.

## Why the PC build, not the Xbox build

| | PC build | Xbox build |
|---|---|---|
| code | 5.58 MB (exe + 16 DLLs) | 4.05 MB `.text`, one static image |
| named entry points | 50,581 (export tables) | **0** (no export table; kernel imports by ordinal) |
| GPU boundary | D3D8 COM API — documented, dxvk-d3d8 exists as reference | NV2A push buffers — this is the xemu problem |
| oracle | already working (C005) | would have to be stood up |

The GPU boundary alone would decide it: replacing D3D8 API calls is tractable
work, whereas the XBE statically links D3D/DSOUND/XGRPH/DOLBY and drives the
hardware directly.

## Feasibility, measured

`InstrHisto.py` counts instructions inside Ghidra-identified function bodies —
*not* a linear sweep, which decodes padding and data as code (objdump reported
29% `nop` and 146 `aas` for libIGDisplay, both artefacts).

| | XMen2.exe | libIGDisplay.dll |
|---|---|---|
| functions identified | 11,106 | 521 |
| exec bytes covered | 2,025,452 / 2,613,248 = **77.5%** | 26,220 / 32,768 = **80.0%** |
| instructions | 643,647 | 8,754 |
| distinct mnemonics | 186 | 54 |
| top-50 mnemonics cover | 98.76% | 99.95% |

The headline worry — that `XMen2.exe` exports nothing and so has the Xbox's
discovery problem — **did not survive measurement.** Coverage of the symbol-free
exe (77.5%) matches the fully-symbolised DLL (80.0%). Symbols supply *names and
types*; boundaries come from analysis, and analysis works.

Instruction mix is benign: the integer core (MOV/PUSH/CALL/LEA/POP/TEST/ADD/JZ/
CMP/JNZ) is 80% of the exe, x87 is 41 mnemonics at 5.83%, SSE is 0.18%. A
decoder covering ~80 mnemonics reaches 99.7%.

**What this does not establish:** that the identified boundaries are *correct*
rather than merely present; what the remaining 22.5% of exec bytes is (data,
padding, or reachable code); and — most importantly — instruction-count coverage
is not semantic coverage. One mis-modelled flag or x87 precision case breaks
execution regardless of histogram share.

## Shape of the work

- **Recompiler** (offline): PE + Ghidra-derived function boundaries → C. One C
  function per identified function, operating on a CPU state struct. Direct
  calls where the target is known; a dispatch table for indirect calls.
- **Generated output**: gitignored, regenerable, never hand-edited.
- **Runtime**: host implementations of the imported Win32 / D3D8 / DirectInput /
  msvcr71 surface (989 named imports total, C001).
- **Overrides**: hand-written native C replacing recompiled functions one at a
  time, with the recompiled body kept alive and A/B-toggleable.
- **Harness**: the recompiled build diffed against the Wine oracle.

### Order

`libIGDisplay.dll` is the first recompiler target — 32 KB, 521 functions, 54
mnemonics — because the drop-in swap and tracing infrastructure for it already
exists and works (C004, C006). It is a proving ground for the recompiler, not a
milestone in itself. `XMen2.exe` is the eventual target.

## What carries over from the reimplementation work

Nothing built so far is wasted:

- `tools/pe.py` — PE sections/imports/exports; the recompiler needs all of it.
- `tools/run_shim.sh` — the Wine oracle (C005), now the differential reference.
- `tools/build_shim.sh` + `tools/gen_trace.py` — drop a replacement DLL into the
  running game and trace the boundary. A recompiled DLL is dropped in exactly
  the same way, and the tracer gives ground-truth call sequences from the
  original to diff a recompiled build against.
- `docs/RE/ark.md` — the ARK meta-object system (C008, C009). Recompiled code
  reproduces it automatically, but overrides need it understood.
- `src/core/` — IGB/DXT/mesh/Enbaya decoders, for asset-level work and overrides.

## Known limits carried forward

- Frame-level A/B is not deterministic yet (boot-movie timing varies run to run).
  Only module-load sets and error logs are currently comparable.
- The harness needs the Lutris prefix (`WINE_PREFIX` in `.env`) for DXVK's d3d8;
  this Wine build ships no builtin d3d8.
- `tools/gen_trace.py`'s "never called" summary never runs, because the harness
  SIGKILLs the game.

## Removing the D3D8 seam

**Decision, 2026-08-26 (the user's): the D3D8 layer is a way station, not a
destination. The port owns the engine's renderer instead, and `src/d3d8/` is
deleted from the top down as that happens.**

The reason is the same one that chose recomp over Wine: *ownership*. A D3D8
emulator is the one part of this tree that is not ownership — it is a
compatibility shim reconstructing intent that the engine had a moment earlier
and threw away. That cost is not theoretical. Stage two of the prompt-glyph
work is stuck precisely there: by the time a glyph reaches D3D8 the engine's
text space is gone, and the plan built on that was to encode an atlas index in
a UV float to smuggle it past the layer that lost it (`RE/text.md`).

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

`libIGGfx` is **5,580 recompiled functions**. The seam cannot be removed by
deciding to; it is removed when its last caller is ours, and until then it is
the only working path. So: port a renderer subsystem natively, watch its D3D8
call sites go to zero, and delete what is then unreachable. Nothing is deleted
speculatively.

**First target: the 2D/UI draw path.** It is the one subsystem that is
separable today — screen-space, unlit, unshaded, no lighting state and no
vertex shader — it is where the seam is actively blocking work, and the port
already has a proved screen-space draw (`d3d8_screen_space_test.c`,
`GpuDraw.pretransformed`).

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
