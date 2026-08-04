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
