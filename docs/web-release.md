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
| `x86port_runtime` — x86 decode, semantics, x87, SIMD, host emission | 34 of 35 translation units |
| `jitcommon` — shared code region and block cache | 1 of 2 |
| Zydis, the pinned decoder | 18 of 18 |
| Zycore, its support layer | 13 of 13 (needs `ZYAN_NO_LIBC`) |
| Bochs software x87/SSE math | 233 of 233 |
| Platform-neutral port owners (touch layout, HUD layout, settings, boot mode, save directory, gameplay control) | 8 of 8 |

So the guest's decode, semantics and software math are already portable, and
the two that are not are exactly the two that matter — each of them a compile
error rather than an opinion:

- `jitcommon/code_memory.cpp`: *"llvm.clear_cache is not supported on wasm"*.
  That is the JIT's executable code region. Gate W1, stated by the compiler.
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

### W1 — There is no execution engine for WebAssembly. *(blocking, upstream)*

`shared/x86port` has exactly two JIT backends and picks between them by host
architecture (`vendor/shared/x86port/CMakeLists.txt`, the
`CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"` branch): `jit_arm64.c` or
`jit_x64.c`. Emscripten reports neither, so it falls into the `else()` and
links the **x86-64** backend, which emits x86-64 machine code into a buffer and
jumps to it. WebAssembly cannot execute a byte buffer. The build would succeed
and the product would be dead on the first translated block.

This is the whole gate. The two ways out and why only one survives:

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

**The encoder half of that is now done and upstream.** `emit_wasm.{h,c}` writes
the WebAssembly binary format, and its oracle is a real engine rather than a
disassembler: `test_emit_wasm` builds twelve modules and node validates each
against the specification and runs it, 12 of 12. `shared/x86port` also no longer
guesses: the backend selection used to treat every non-ARM64 host as x86-64, so
an Emscripten configure quietly linked an emitter this host cannot run. It now
refuses by name, and this port's measurement passes
`-DX86P_MEASURE_UNRUNNABLE_BACKEND=ON` to keep measuring, told on every
configure that the library it gets cannot execute a guest instruction.

What remains for W1 is the lowering — `jit_wasm.c`, guest block to module — plus
module lifetime: an instantiated module is permanent, so per-block modules leak
without bound and block-cache eviction becomes a memory-correctness requirement
rather than a tuning knob. `shared/x86port`'s `docs/migration.md` Gate 8 owns
the ordered work.

Two browser constraints shape that backend and must be settled before it is
written, not after:

1. Synchronous `new WebAssembly.Module(bytes)` is limited to small modules on
   the main thread. Off the main thread it is unrestricted — which is the same
   place W3 already puts the game loop. Compiling on a worker is therefore not
   an extra cost; it is the design.
2. Every instantiated module is permanent. A per-block module for a long
   session is an unbounded leak of module objects, so block-cache eviction
   becomes a memory-correctness requirement rather than a tuning knob, and
   blocks want batching into larger modules.

Falsified by: a `jit_wasm` backend that translates and runs x86port's own JIT
test suite under node, with its refusal counters reported.

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
