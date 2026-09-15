# Web release (WASM + PWA)

The browser product uses the same native game owners and x86port runtime as the
desktop and Android packages. Players select their own complete PC-install ZIP;
files stream into origin-private storage and never leave the device. Only the
port executable, shaders, UI and other redistributable resources enter a release.
[Project state S021](project-state.md#s021--web-wasm--pwa-product-with-browser-side-install-partial)
owns current capability status. The central Pages route serves the build from
xmen2 run 34852326627 at commit 011326b, published into `~/repo/pages` as
`2f4d21f` and verified live: `publication.json` at the route names that commit
and run, and the served `x2native.wasm` (sha256 `5f229cf5...`) and `app.mjs`
(sha256 `c9dfb81b...`) are byte-identical to the reviewed artifact. It remains a
preview: responsive, visible gameplay has not passed, and the deployable
capability is still `partial`.

## Build and packaging owners

`tools/build_web.py` configures Emscripten 4.0.16 and Ninja under `build/web`,
using the locked project Python interpreter. `shared/web-port` owns the exact
SDL3 WebGPU, SDL_image, SDL_ttf, FreeType, zlib, bzip2 and FFmpeg prefix. Its
manifest rejects missing libraries and stale build recipes. Titles do not carry
private copies of this dependency builder.

```
uv run --frozen python tools/build_web.py --emsdk PATH_TO_EMSDK
```

`--skip-deps` checks the existing shared prefix before using it; it does not
bypass dependency validation. `--configure-only` does not compile or package.
`cmake/WebDependencies.cmake` consumes the prefix and `cmake/WebTarget.cmake`
composes the worker, OPFS, canvas and memory options. `cmake/Shaders.cmake`
uses the shared deterministic WGSL converter for browser shaders.

After a successful link the builder passes an explicit resource map to
`shared/web-port/tools/package.py`, producing `build/release/web`. The packager
refuses unexpected files in that directory and never discovers game inputs by
walking a build tree. `shared/web-port` supplies browser storage and isolation;
its generated service worker caches only the resource map, refuses any body
that does not match the release hash, and activates without waiting for open
clients, so a shipped fix reaches a returning page instead of a stale cache. The
source workflow
uploads that directory as an asset-free CI artifact. The `pages` repository
imports it under `public/xmen2/` and owns the GitHub Pages deployment, including
`.nojekyll`, with no game ZIP, executable, save or translation cache uploaded.

## Runtime contracts

### W1 — Execute emitted guest blocks

Pinned x86port `5f9bb3daf6fee6fbeab9212260e0f433a39d5b1e` provides the real
WebAssembly module host, indirect-table publication, dispatcher, imports,
cache release, sparse permissions, integer-tail, x87 and SIMD lowering. Eight
synthetic suites passed in standalone and application-worker Emscripten
configurations, in addition to 43 native tests. The threaded Emscripten
matrix passed 45 checks with six declared host-only skips. The title consumes this
reviewed immutable revision and maps browser guest allocations through the
same permission-aware owner.

Every cold executable block is offered to the JIT first. Unsupported operations
must carry a reason and counted bounded fallback. A title run with zero JIT
execution or dominated by fallback cannot establish gameplay or performance.
Runtime discovery reads the player's own PE images; no pretranslated game
corpus or interpreter-first player mode is permitted.

### W2 — Present the real renderer

The maintained SDL fork presents through WebGPU from an application worker.
Observed synthetic evidence includes device creation, render-pass submission,
fence/download, raw depth sampling and a visible blue SDL canvas. These are
backend contracts, not game-frame evidence.

The title converts its four shaders and the pinned RmlUi backend's three shaders
to WGSL. Texture/sampler slots and depth-image declarations must match the SDL
backend's resource contract. Acceptance requires a real X-Men frame, settings
and touch UI, audio and input through the same owners as desktop.

The `present_luma` instrument (`src/gpu/gpu_present_luma.c`, armed
`--set present_luma=N` or `X2_PRESENT_LUMA`) reads back BOTH the logical D3D
scene and the composed windowed frame, so a run can be told to photograph what
actually reaches the screen instead of trusting a page screenshot of a
worker-owned OffscreenCanvas. Measured with it (issue #152): the browser is
genuinely black, and the black is in the composite step -- the scene holds
content (`max 191`) while the aspect-fit composite yields exactly `0` at 1:1
sizes, and `--vk-selftest` hangs at its first composite fence on WebGPU though
it passes on native. This localizes the W2 gap to the SDL-WebGPU fork's
blit/fence path, not the game-to-GPU draw path.

### W3 — Worker execution, canvas and guest memory

The browser main thread owns DOM and events. Emscripten proxies the native entry
to a pthread, transfers `#canvas` with OffscreenCanvas, and enables Asyncify for
SDL WebGPU's synchronous native waits over asynchronous browser operations.
Both the JIT and OPFS native calls run off the browser main thread. Guest
threads are serialized by the one guest mutex, and under Emscripten a voluntary
release cannot rely on `sched_yield()` to deliver a turn (it is a worker no-op,
and the releasing thread re-wins the lock's trylock): `src/native/threads_yield.c`
makes the quantum yield and `Sleep(0)` a bounded promise that another guest
thread takes the lock before the yielder re-takes it. Without it woken waits
advanced only when the holder parked, which measured 178-361 ms per
`WaitForSingleObject` and stalled the menu/movie phase (issue #149).
The application unmounts OPFS on its worker after returning from setup or native
main, releasing browser-backed file handles before SDK runtime destruction.
Stored files remain available on the next mount. Emscripten 4.0.16 still joins
the OPFS backend worker from its global destructor on the browser main thread;
assertions expose this remaining SDK lifecycle warning. Unmount fixes file-handle
teardown but does not establish fully nonblocking runtime shutdown.

Wasm memory is shared and may grow up to 4 GiB. The initial capacity is 64 MiB;
the measured title static data plus main stack already requires about 31 MiB.
This is host heap capacity, not a guest address-space reservation. The title's
sparse backend maps disjoint guest ranges through x86port, preserving permissions,
partial decommit, exact-span reads and invalidation instead of reporting no-op
page protection as success.

GitHub Pages cannot set response headers itself. The shared web-port service worker adds
COOP/COEP to the exact cached application resources. The first navigation
registers it and reloads once; a persistent inability to isolate produces an
explicit refusal. A real browser test on a server without those headers reached
`crossOriginIsolated=true`, then loaded again with the server stopped. No
browser-support claim follows until the complete title works under this policy.

The browser entry point uses page keyboard and touch events rather than the
desktop loopback HTTP inspector; a browser worker cannot bind that socket.
The asset-free package has first-run ZIP refusal and storage/isolation checks.
A complete PC install was imported and reopened from private storage locally;
responsive gameplay remains unqualified.

A stop in the browser used to arrive as "native code called abort()" and
nothing else. x86port reports through one process-wide sink whose default
writes to standard error, and a worker's standard error never reaches the page,
so every fatal library diagnostic was silent there. The port installs its own
sink (`src/native/x86_engine_diagnostic.c`): the component and the text arrive
in the page's console and the guest state is dumped before the library aborts.
The page's console is written in blocks rather than one proxied round trip per
line, and the sink states its own cost, because the log competes with the guest
for the same pthread.

### W4 — Local install, storage and offline use

`web/app.mjs` owns the title page, file chooser and lifetime lock; shared web-port owns
bounded Blob-to-OPFS staging. The application lock prevents another tab from
replacing an installation while native workers use it. A fresh page releases
only its known abandoned staging leaf before accepting another file.

`src/web/web_main.cpp` mounts OPFS through web-port and delegates ZIP safety to Lucent and
complete-install validation to the title. WasmFS OPFS cannot rename a directory,
so the browser extracts into a fresh unpublished install directory and writes
an `install.ready` file only after validation. Saved play requires that marker;
an interrupted extraction is removed on the next setup visit. Leaves
written by the retired atomic extraction (`install.preparing*`,
`install.previous`) are cleared before importing: they held whole trees when the
staging rename failed on OPFS, and dead bytes compete for the quota the new
install needs. Lucent's ZIP
owner uses bounded reads and streaming decompression: mmap or copying a
multi-gigabyte archive into wasm linear memory is not acceptable.

The browser's measured storage quota is too small to hold the full ZIP plus two
expanded installations. Replacing an existing install therefore discards the old
one after the new ZIP has copied successfully, before extraction. A failed
replacement requires importing again. This is an explicit limitation of the
current browser route, not evidence that replacement preserves a prior install.

Persistence grants are requested and their refusal is visible. OPFS availability
is not a guarantee against eviction. Acceptance includes a real complete-install
import, invalid/corrupt input refusal, explicit replacement behavior, progress
during import, saves/settings across reload, and offline relaunch of the
installed app. Merely caching the launcher is not offline game evidence.

After a saved install is present, **Start Dead Zone gameplay test** requests
`act1/deadzone/deadzone1` through the title's existing `X2_BOOT_MAP` runtime
owner. It runs the retail `startFirstMission` party initializer before loading
the map. This is a diagnostic boot route for measuring interactive gameplay;
the ordinary **Play saved installation** route still follows the retail intro.
The browser reached the retail boot hook for the Dead Zone test map and emitted
scene draws, but the canvas remained black and frame times were unplayable.
Neither route is release-qualified.

### W5 — Floating-point semantics

WebAssembly has no host floating-point environment matching x87. The shared
software x87 path supplies rounding and exception semantics; the pinned software
suite passed 3,140 checks. Emitting an x87 guest block through WASM is a separate
W1 contract and must be tested through actual published modules.

### The page's command line

A browser page has no environment and no argv, so `web/app.mjs` builds the
runtime's arguments: a repeated `?arg=` becomes one argument, so
`?arg=--set&arg=hotep=4096` arms the hot-entry-point probe in a packaged build.
The web entry point consumes only its own route requests (`--import`,
`--test-deadzone`, classified by `src/web/web_request.cpp`) and forwards
everything else to the native option parser, which refuses an unknown flag by
name rather than ignoring it. The classifier scans the whole line, not just
`argv[1]`, because the page appends its `?arg=` diagnostics after the route
request: a prior `argc == 2` test dropped `--test-deadzone` under any appended
diagnostic and booted the retail intro instead of the requested map (issue
#151). The port's diagnostics are therefore reachable in
the browser as they are on a phone (through the Android bridge setting
`X2_HOTEP`) and on a desktop (`--set`). Before that forwarding existed the page
was a launcher whose command line nothing read, and "where does this frame's
time go" had no answer in the browser at all.

## Release verification

Shared synthetic tests prove individual engine and browser contracts. Local
real-title observation proves that the product reaches those owners using the
player's input. Report translated blocks/instructions and fallback denominators
while running, together with rendering, audio, input, save and memory evidence.
Do not convert an input script into a gameplay gate or call a loading-only page
a shipped game. Hosted CI and release artifacts contain no copyrighted assets.

`tools/wasm_portability.py` is the shared-runtime census, not a browser product
gate. It records compiled translation-unit denominators for the pinned
WebAssembly backend; title install, rendering, gameplay, and lifecycle evidence
come from the packaged Pages artifact and a real browser run.
