# Strategy: recomp vs reimplementation — decided, with the measurements

**Decision: reimplementation, driven by a per-DLL differential harness. Static
recompilation is rejected, and it is rejected on evidence, not taste.**

The measurements behind this live in `docs/info/claims/` (C001–C005) with their
falsifiers. Reproduce any of them with `tools/pe.py` and `tools/run_shim.sh`.

## Why not a recomp

Recompilation pays for itself when the source ISA is one you cannot execute —
that is the whole reason N64 and PS2 recomps exist. **Both shipped builds of this
game are already x86:** the PC build is 32-bit PE, and the Xbox build is a 32-bit
XBE. There is no alien ISA to escape, and the mature tool for running x86 PE code
on Linux (Wine) already exists and, as of this session, demonstrably runs the
game. A recomp would spend years rebuilding a worse Wine.

The x86-specific difficulties are real too — variable-length instructions make
code/data separation undecidable, and this engine dispatches through C++ vtables
everywhere (150 exported vftables in `libIGDisplay` alone) — but they are not the
main argument. The main argument is that a successful recomp would produce
something we can already do.

## Why the reimplementation is bounded

The engine's DLLs export **49,357** symbols in total, which sounds like an
open-ended rewrite. It is not, because `XMen2.exe` only ever calls **794** of
them:

| module | symbols the exe imports |
|---|---|
| libIGSg | 290 |
| libIGCore | 160 |
| libIGAttrs | 134 |
| libIGMath | 125 |
| libIGGfx | 61 |
| libIGUtils | 15 |
| libIGDisplay | 9 |

Every symbol is exported **by name**, MSVC-mangled, so each one carries its full
C++ signature — and the Alchemy 5.0 headers in `scratch/ref/alchemy5/` give the
class model behind them. That is a specification, not a guess.

## The harness: swap one DLL at a time

The trap in a from-scratch reimplementation is that nothing runs until everything
runs, so there is no oracle and no way to know a subsystem is wrong until the
whole thing boots. The fix is to keep the original game as the test harness:

1. Run the original `XMen2.exe` under Wine — this works today,
   headless and capturable (`tools/run_shim.sh`).
2. Build our engine as a PE DLL that exports the *exact* mangled names, and drop
   it in for one `libIG*.dll`. **Everything else stays original**, so any
   divergence is localised to the DLL just swapped.
3. Repeat per DLL. When the last one is native, only `XMen2.exe` is still
   original; reimplement it last, against an engine that already works.

Wine is scaffolding for the *harness*, not a dependency of the *product*. The
shipped result is still the native SDL engine, which needs no Wine.

**Start with `libIGDisplay`.** Nothing else in the program imports more than 24
symbols from it (the exe uses 9; Gui, Viewer, Insight, Audio and Movie account
for the rest), it is the smallest replaceable module, and it is exactly where all
three target features live — controller hotswap, auto-mapping, Xbox prompts.
By comparison `libIGCore` has 784 inbound symbols and must come last.

## Status

Two shims exist, both reproducible with `tools/build_shim.sh <mode> <dll>`:

- **`proxy`** — all 898 exports forwarded to the original. **Verified transparent
  in the real game** (C004): the swap mechanism works end to end before a single
  line of behaviour is reimplemented.
- **`trace`** — the 22 code symbols of the boundary surface routed through naked
  asm thunks that log the call and then `jmp` to the original. This retires the
  biggest risk to the whole strategy (C006): **mingw-built code receives real
  MSVC `__thiscall` calls from the game and passes them through intact**, with
  the game running past the splash into the intro cinematic.

The tracer also gave the boot order of the display layer:

    igWindow::getClassTypeLazy
    igWindow::arkRegister
    igWindow::_instantiateRefFromPool -> _instantiateFromPool
    igWin32Window::hideCursor
    igDefaultInterfaceManager::_instantiateRefFromPool -> _instantiateFromPool
    igControllerManager::_instantiateRefFromPool -> _instantiateFromPool

`_instantiateFromPool` is the wedge. An export proxy cannot intercept virtual
dispatch or calls internal to the original DLL (C007), so behaviour is replaced
by owning **construction**, not by hooking methods — which is why whole classes
are the unit of replacement.

**ARK is decoded** — see [`docs/RE/ark.md`](RE/ark.md). Class registration is a
single 11-argument `igArkRegister` call into libIGCore; construction is entirely
delegated to `igMetaObject::createInstance`; and an abstract class points at its
platform implementation through `igMetaObject+0x3c`, which `createInstance`
follows in a loop.

Two consequences change the plan for the better:

- **`_Meta+0x3c` is a supported substitution point.** `igControllerManager`
  writes `igWin32ControllerManager::getClassMetaSafe` there, and `igWindow`
  writes `igWin32Window`'s. Repointing it redirects every instantiation of the
  abstraction to a different concrete class — so a native
  `igSDLControllerManager` can be substituted without replacing `libIGDisplay`
  wholesale.
- **MSVC vtable placement does not have to be reproduced.** Alchemy captures a
  class's vtable pointer through a `retrieveVTablePointer` hook that discovers
  the vptr offset at runtime; libIGCore stamps that pointer into every instance.
  Hand-rolled C vtables suffice. Slot *order* within the vtable is still a
  constraint and still has to be read out of the binary.

The frontier is now `vtable` (slot order) and `constructderived` (how libIGCore
finishes an object). The ARK mechanism is **read, not yet exercised** — nothing
has been registered by our own code.

### Known limits of the harness, honestly

- **Frame-level A/B is not deterministic yet.** The boot movies advance at
  different rates under llvmpipe, so two runs land on different frames. Only
  module-load sets and error logs are currently comparable. A deterministic
  scenario (fixed frame count, seeded RNG, scripted input) is still to be built.
- **Only boot and the intro cinematic have been exercised.** Nothing
  interactive has been driven, so transparency is proven for load and early init
  only.
- **"Never called" is not yet measurable.** The tracer's end-of-run summary runs
  on `DLL_PROCESS_DETACH`, which never happens because the harness SIGKILLs the
  game — so a symbol's absence from the trace does not distinguish *never called*
  from *not called yet*.
- The harness needs the Lutris prefix (`WINE_PREFIX` in `.env`) for DXVK's
  d3d8; this Wine build ships no builtin d3d8 at all.

## What is explicitly NOT being done

- No static recompilation of `XMen2.exe` or the DLLs.
- No emulation of the Xbox build; `assetsfb.wad` is an *asset* source only.
- No shipping dependency on Wine.
