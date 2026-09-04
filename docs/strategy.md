# Strategy: run the guest at runtime + native overrides

**Decision: execute the PC build's own machine code at runtime, and replace
functions with hand-written native code incrementally. Target is the PC build,
not the Xbox build.**

The gameplay product has one execution architecture: native subsystem overrides
plus `shared/x86port` runtime JIT execution for every remaining guest path.
Explicit interpreter mode is a separately built x86port test oracle. The
product may enter only a bounded, counted interpreter fallback when compilation
fails or is unsupported, or when emitted code is unsafe to execute; fallback
intervals do not establish gameplay or performance conformance.

The PC release is the conformance target. Its D3D8 boundary, exported engine
surface, and working Wine control make incremental native ownership measurable.
Xbox-derived controller observations remain evidence only.

Measurements and their falsifiers live in `docs/info/claims/`; active capability
state and remaining product gates live in `docs/project-state.md`.

## Runtime contract

The runtime maps authenticated PE images supplied by the player, translates
reached x86-32 blocks on demand, and hands imports or registered overrides to the
title dispatcher. Unsupported instructions either refuse by name or enter the
bounded, instrumented fallback above; unresolved calls still refuse by name.
Native overrides may call their original guest body through the same JIT without
recursion. No player-facing execution-engine selector exists.

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
- **Test oracle**: explicit interpreter mode is linked only into a separately
  built test target used for focused differential diagnosis. Product
  configuration cannot select it. The internal bounded fallback has no
  player-facing selector and reports every entry and executed interval.
- **Shared Alchemy foundations (partial)**: `shared/alchemy` owns neutral
  format/render-data and input libraries plus XMLB/ARK tools. `x2native`
  separately composes `alchemy::input` and `x86port_runtime`; the first
  title-local controller adapter publishes one latched sample to the shared
  state owner and A/B checks it against retained DirectInput. The current
  runtime frontier still stops before gameplay input, so callback/hotplug
  conformance remains open.
- **Harness**: the port diffed against the Wine oracle, plus the port's own
  control channel for driving a running build.

### Order

Every module is executed through runtime translation from the first frame.
Before further native-renderer work is
treated as a landed product milestone, complete these architecture gates:

1. Keep both source/selector and binary link-closure halves of the runtime
   boundary audit in the normal integrated-product verifier; both pass now.
2. Exercise both a native override and its scoped original-body JIT call.
3. Publish the consumer-proven revision already present in the canonical
   `shared/x86port` checkout, reconcile this project to that immutable remote
   revision, and keep title-local CPU semantics out of this repository.
4. Run a bounded representative interactive gameplay scenario with native
   overrides active, nonzero JIT blocks, and independent CPU/memory/timing/
   device evidence. Boot, menu, FMV, and unattended loops are checkpoints only.
5. Qualify each declared host backend. x86-64 exists; Apple Silicon and Android
   ARM64 require a real JIT backend. Bounded per-block fallback cannot substitute
   for a missing architecture backend.
6. Integrate `alchemy_input` first through a narrow guest
   `igControllerManager` adapter, A/B-verified against the retained DirectInput
   path as specified by `shared/alchemy/docs/input.md`. Extend further shared
   engine behavior only after X-Men 2 shipping-path evidence proves the
   contract title-neutral. Do not start MUA migration until every X-Men 2 goal
   is verified; then migrate MUA without rewriting its gameplay.

Native override work then continues at the D3D8 seam described below.

## What carries over

- Runtime PE image identity, sections, imports, exports, and relocations remain
  necessary loader and RE facts. They do not become an ahead-of-time function map.
- The unmodified retail PC build under Wine remains an independent behavioral
  oracle (C005). It is not a generated or replacement-DLL product mode.
- Boundary observations already recorded from the retail binary remain valid
  evidence when their falsifiers still hold.
- `docs/RE/ark.md` — the ARK meta-object system (C008, C009). The guest's own
  code reproduces it automatically; overrides need it understood.
- Xbox-derived controller and behavioral observations remain evidence only.

## Known limits carried forward

- Frame-level A/B is not deterministic yet (boot-movie timing varies run to run).
  Only module-load sets and error logs are currently comparable.
- The harness needs the Lutris prefix (`WINE_PREFIX` in `.env`) for DXVK's d3d8;
  this Wine build ships no builtin d3d8.
- The product binary-link audit passes. Remaining runtime-emitter coverage,
  representative interactive gameplay, canonical x86port publication/pin
  reconciliation, and ARM64 backend qualification are still open; see
  `docs/project-state.md` S001, S002, S010, S014, and S018.

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
