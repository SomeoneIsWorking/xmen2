# Web release (WASM + PWA)

The web target is a Progressive Web App: the player opens a URL, installs it
like an app, points it at **their own** game ZIP, and plays. The page ships no
game files, and the ZIP never leaves the device — it is unpacked into
origin-private storage in the browser, exactly as the AppImage and the APK
unpack it into a user directory. This is the same no-terminal setup rule the
other two packages already follow, with the browser as the host.

Touch play is already portable ([touch-play.md](touch-play.md), S020), so a
phone or tablet opening the PWA gets the on-screen pad for the same reason a
Windows tablet running the desktop build does: it is touching the screen. That
is a consequence of the touch owners being platform-neutral, not new web work.

**This document is a plan and a set of gates, not a capability claim.** Nothing
here has been observed running. `docs/project-state.md` S021 is the state of
record.

## What was measured

Emscripten 4.0.16 was pointed at the real CMake projects and the results counted
rather than estimated. `tools/wasm_portability.py` re-runs this and is wired
into the ordinary suite (skipping, never passing, without a toolchain) and into
CI as the `web-wasm` job:

| Component | Compiles to wasm32 |
|---|---|
| `x86port_runtime` — x86 decode, semantics, x87, SIMD, host emission | 38 of 39 translation units |
| `jitcommon` — shared code region and block cache | 1 of 2 |
| Zydis, the pinned decoder | 18 of 18 |
| Zycore, its support layer | 13 of 13 (needs `ZYAN_NO_LIBC`) |
| Bochs software x87/SSE math | 233 of 233 |
| Platform-neutral port owners (touch layout, HUD layout, settings, boot mode, save directory, gameplay control) | 8 of 8 |

Those 38 now include the whole WebAssembly backend — the encoder, the module
builder, guest-state access, the lowering, module lifetime and the adapter, ten
files — compiled by Emscripten's clang under `-Wall -Wextra -Werror` with no
warnings, and no x86-64 emitter object anywhere in the build. That is the first
time any of it has been through a real wasm32 compiler, and it is what makes
`jit_wasm.c`'s `_Static_assert(sizeof(void *) == 4)` a checked fact rather than
an assumption.

So the guest's decode, semantics and software math are already portable, and
the two that are not are exactly the two that matter — each of them a compile
error rather than an opinion:

- `jitcommon/code_memory.cpp`: *"llvm.clear_cache is not supported on wasm"*.
  That is the JIT's executable code region, and it stays unportable on purpose:
  the WebAssembly backend produces a module rather than a byte buffer, so it
  needs no code region at all. What this compile error now marks is W1 item 2 —
  the dispatcher still routes every translation through that region, and the
  wasm path has to fork before it.
- `x86port/x87.c`: *"'#pragma FENV_ACCESS' is not supported on this target"*.
  Gate W5 below, which the plan did not have before it was measured.

One build-configuration finding: Zycore recognises Emscripten and deliberately
gives it no POSIX layer, so its process/memory/terminal/thread sources refuse
to compile. `ZYAN_NO_LIBC=ON` skips them, and the decoder — which is all this
project uses Zydis for — needs none of them.

## What already fits, and what that is worth

Three things were checked rather than assumed:

- **Guest memory is not tied to `mmap`.** `src/native/guest_memory.c` reserves
  a 4 GB arena and rebases every guest address into it, and
  `X2_GUEST_ARENA_RESERVED` already forces that mode on a host that refuses the
  low 4 GB one-to-one — which is why Apple Silicon and Android work. A wasm32
  linear memory is such a host. What is NOT solved: the reserved path still
  calls `mprotect` for Win32 page protection, and WebAssembly has no page
  protection at all, so `apply_host_protection` needs a wasm answer (most
  likely: track the guest page state and refuse in software, which the file
  already models) rather than a silent no-op that reports success.
- **The install contract is reusable.** `install_validation` requires every
  loader PE image plus title content sentinels before a selection is retained.
  The browser is a third front end onto that same rule; it must not grow a
  looser one.
- **Touch, settings, config location and save location are already owners**
  rather than platform code, so the web build consumes them.

None of that is the hard part.

## The gates, in order

Each gate names its evidence and what would falsify it. A gate is not "hard
work remaining" — it is a thing that does not exist, where the build currently
succeeds while producing something that cannot run.

### W1 — Nothing instantiates or enters a translated block. *(blocking, upstream)*

`shared/x86port` used to have exactly two JIT backends and pick between them by
host architecture: `jit_arm64.c` or `jit_x64.c`. Emscripten reported neither, so
it fell into the `else()` and linked the **x86-64** backend, which emits x86-64
machine code into a buffer and jumps to it. WebAssembly cannot execute a byte
buffer, so the build succeeded and the product was dead on the first translated
block.

That is no longer the shape of the gate — see the status below — but the gate is
not closed. This is what it was, and why only one of the two ways out survives:

- *Run the interpreter instead.* Refused. AGENTS.md: the gameplay target always
  uses the JIT, product configuration cannot choose the execution architecture,
  and a fallback-backed run cannot establish gameplay or performance
  conformance. Independently of the rule, a 2005 3D action game under an x86
  interpreter inside a browser would not be playable, so this would ship the
  "smaller product that looks like progress" the same document forbids.
- *Add a WebAssembly backend.* `emit_wasm.c` + `jit_wasm.c` in `shared/x86port`,
  alongside the two that exist: translate a guest block to a WebAssembly module
  and instantiate it. This is the proven design for x86 in a browser, and it
  fits the existing contract — the backend modules already implement one
  externally-visible `x86p_jit_*` interface, chosen once at configure time.

**Status: a third backend now exists, and Emscripten selects it.** Upstream
`shared/x86port` (pinned at `6ee313b`) has, in order:

- **The encoder.** `emit_wasm.{h,c}` writes the WebAssembly binary format, and
  its oracle is a real engine rather than a disassembler: `test_emit_wasm`
  builds twelve modules and node validates each against the specification and
  runs it, 12 of 12.
- **The lowering.** `jit_wasm_lower.c` and five per-family units turn a guest
  block into a module: MOV/MOVZX/MOVSX/LEA/XCHG/SETcc/PUSH/POP/LEAVE/CDQ/CWDE/
  CLD/STD, the inline ALU shapes (ADD, SUB, CMP, OR, AND, TEST, XOR, NOT), the
  helper-backed ones (ADC, SBB, NEG, INC, DEC, every shift and rotate) and the
  branches (JMP, Jcc, JECXZ, CALL, RET). `test_jit_wasm` translates 38 blocks,
  hands each to node, and compares the whole `X86pCpu` and all of guest memory
  against the interpreter — 38 of 38 blocks reached the engine, 1,097 checks, 0
  divergences. Twelve of thirteen deliberate mutations to the backend were
  caught by that corpus; the thirteenth is recorded as unobservable at the site,
  not quietly dropped.
- **Module lifetime.** `jit_wasm_arena.c` owns published modules against a host
  interface, caps them at 1,024, and *refuses by name* past the cap rather than
  evicting — because the block cache holds entry addresses the arena handed out
  and does not consult it before entering one. 32 checks through a stub engine.
- **Backend selection.** Emscripten is now asked about *before* the processor is
  looked at, because Emscripten reports its processor as `x86` — which is how it
  got the x86-64 emitter in the first place. A host with no backend still
  refuses by name.

And it compiles: the wasm32 measurement above builds all ten of those files with
Emscripten's own clang, warnings-as-errors, clean.

What remains for W1, and none of it is small:

1. **Nothing instantiates a module.** `X86pWasmHost` is an interface with no
   implementation; the only thing that has ever implemented it is a test stub.
   The real one is JavaScript glue plus an `EM_JS`/`emscripten_run_script`
   boundary, and Emscripten function pointers are indirect-table indices, which
   is the mechanism a resolved export has to come back as.
2. **The dispatcher has no publication edge.** `jit_engine.c` translates into a
   code region and enters it; the wasm path produces a module and an export
   name instead, and that fork does not exist yet.
3. **The instruction set is a first slice.** No string operations, no
   multiply/divide, no SIMD, no x87, no `LOOP`, no double shifts — each of which
   the x64 backend has and the guest uses.
4. **Compile off the main thread**, per the constraint below, which ties W1 to
   W3.
5. **Eviction is deliberately absent**, so a long session hits the cap and gets
   a named refusal. Closing that means the block cache telling the arena when it
   discards a block — a change on the cache side, not here.

`shared/x86port`'s `docs/migration.md` Gate 8 owns the ordered work.

Two browser constraints shaped that backend. One is answered in its structure;
the other is still open and belongs to the glue that does not exist:

1. **Open.** Synchronous `new WebAssembly.Module(bytes)` is limited to small
   modules on the main thread. Off the main thread it is unrestricted — which is
   the same place W3 already puts the game loop. Compiling on a worker is
   therefore not an extra cost; it is the design, and nothing yet implements it.
2. **Answered in the design, not yet exercised.** Every instantiated module is
   permanent, so per-block modules are an unbounded leak. That is why the module
   builder takes its function count up front and can carry several blocks in one
   module, and why the arena counts what is live, caps it, and refuses by name
   instead of evicting behind the cache's back. What is *not* done: the block
   cache never tells the arena that a block was discarded, and nothing batches
   blocks yet — so today one block is one module, and a long session would reach
   the cap and be refused.

Falsified by: an Emscripten build of this port that reports a nonzero count of
translated blocks executed in the browser, with its refusal counters reported.
The backend's own test suite already runs under node, so that half is evidence
already in hand and no longer the thing being asked for.

### W2 — There is no renderer backend for the web. *(blocking, upstream)*

`src/gpu/gpu_device.c` creates the device with `SDL_CreateGPUDevice` requesting
`SDL_GPU_SHADERFORMAT_SPIRV` only. SDL_GPU's backends are Vulkan, Metal and
D3D12; the pinned revision has no WebGPU backend, and a browser offers neither
SPIR-V nor any of those three. The device creation fails and the product
refuses — correctly, and with no way forward.

Ways out, in preference order: an SDL_GPU WebGPU backend upstream (shaders
would then need a second format alongside SPIR-V), or a second host-GPU backend
owned here against WebGL2/WebGPU directly. The second is a large amount of
duplicated work in `src/gpu/`, so W2 is the gate most worth waiting on upstream
rather than solving locally.

Falsified by: a run reporting a created GPU device with a named web backend.

### W3 — Guest threads, and the browser's one main thread. *(blocking, local)*

The guest is genuinely multi-threaded: `src/native/threads.c` creates real
pthreads for guest threads, `kernel32_wait.c` blocks on them, and libCriMovie
alone runs six workers. Under Emscripten that means:

- pthreads require `SharedArrayBuffer`, which requires the page to be
  cross-origin isolated (`Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp`). For a PWA those headers must
  come from the hosting origin, or from the service worker on every response it
  serves. That is a hosting requirement, and it belongs in the deployment doc
  rather than being discovered at runtime.
- The guest blocks. The browser main thread must never block, so the game loop
  runs on a worker (`-sPROXY_TO_PTHREAD`) and the main thread does input,
  presentation and storage only. This also gives W1 its unrestricted
  synchronous module compilation.

Falsified by: a wasm build that starts the guest's module-init path on a worker
and reports thread creation and wait counters with denominators.

### W4 — The player's ZIP, in a browser. *(local, not blocking the others)*

The setup flow the player sees is the same one the AppImage and the APK show,
with a file input instead of a Browse dialog: choose the ZIP or the install
folder, validate it against `install_validation`'s existing requirement set,
unpack it into OPFS (the Origin Private File System), and remember it so the
second launch goes straight to the game.

What has to be true, and is not free:

- The ZIP is around 1.5 GB. It must be read and unpacked as a stream — never
  held in memory whole — and the storage must be requested as persistent, or
  the browser may evict a 1.5 GB install between sessions with no warning.
- The failure the APK already found applies here: a loader-only selection must
  be refused rather than retained, and the refusal must name what was missing.
- Nothing copyrighted is served by the site. That is not a policy note; it is
  the reason this target is legitimate at all, and any design that caches game
  bytes anywhere but the player's own origin storage is out of scope.

Falsified by: the browser setup page validating and staging a real install, and
refusing an incomplete one by name.

### W5 — WebAssembly has no floating-point environment. *(local, found by measuring)*

`x86port/x87.c` uses `#pragma FENV_ACCESS`, which clang refuses for wasm.
Underneath the pragma is the real problem: WebAssembly has no rounding-mode
control and no floating-point exception flags. The guest's x87 control word
selects a rounding mode and the game can read back exception state, so a wasm
build cannot delegate any of that to host floating point.

The way out already exists and already compiles: the pinned Bochs software
float path builds cleanly to wasm32 in full (233 of 233 translation units), so
a wasm build routes x87 through software rather than through host FP. What is
not free is that the host-FP fast paths in `x87.c` need a compile-time
alternative rather than a runtime one, and the cost of software-only x87 on top
of a wasm JIT is unmeasured — it is a performance risk to size early, not a
correctness gap.

This gate was not in the plan until the toolchain was actually run, which is the
argument for the CI job below existing while the target is still blocked.

## CI

CI's job on this target is to keep the wasm toolchain honest while W1–W3 are
open, and to refuse rather than pass when it has measured nothing.
`tools/wasm_portability.py` is that job, run as `tools/ci.py wasm-portability
--target web-wasm`.

The rules it enforces:

- Every component reports built/total. A component whose total is zero fails:
  a pass that measured nothing proves nothing.
- The two unportable files are pinned by name **with the compiler error that
  explains them**. A third one appearing fails. One of them starting to compile
  also fails, asking for the progress to be recorded — the same ratchet
  `check_structure.py` uses, for the same reason.
- With no Emscripten toolchain the check skips locally (CTest 77) and **fails**
  in CI (`--require-emsdk`), because a CI job that quietly measured nothing is
  the failure mode this exists to prevent.
- The workflow policy itself refuses a web job that runs the policy without the
  measurement, and refuses any `native-components --target web-wasm`, which
  would claim a product that does not exist.

Neither a green tick nor a skipped job may ever be readable as "the web build
works" — that claim is S021's, and S021 says it does not.

## Non-goals

- No game files, ever, on any origin the project controls.
- No web-only "demo" that runs a subset of the game to look like progress. The
  product is the same product.
- No interpreter-backed player build (W1).
- No claim of web support in the README, the release workflow or the state
  document until W1–W3 are closed with observed runs.
