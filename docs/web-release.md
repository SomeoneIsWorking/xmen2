# Web release (WASM + PWA)

The browser product uses the same native game owners and x86port runtime as the
desktop and Android packages. Players select their own complete PC-install ZIP;
files stream into origin-private storage and never leave the device. Only the
port executable, shaders, UI and other redistributable resources enter a release.
[Project state S021](project-state.md#s021--web-wasm--pwa-product-with-browser-side-install-partial)
owns current capability status. The asset-free Pages deployment is verified; title
gameplay with imported game files remains unqualified.

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
walking a build tree. Lucent supplies the browser storage and isolation runtime;
its generated service worker caches only the resource map. The source workflow
uploads that directory as an asset-free CI artifact. The `pages` repository
imports it under `public/xmen2/` and owns the GitHub Pages deployment, including
`.nojekyll`, with no game ZIP, executable, save or translation cache uploaded.

## Runtime contracts

### W1 — Execute emitted guest blocks

Pinned x86port `2607945babec9ec667b5eef6b54232b704860cb7` provides the real
WebAssembly module host, indirect-table publication, dispatcher, imports,
cache release, sparse permissions, integer-tail, x87 and SIMD lowering. Eight
synthetic suites passed in standalone and application-worker Emscripten
configurations, in addition to 41 native tests. The title consumes this
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

### W3 — Worker execution, canvas and guest memory

The browser main thread owns DOM and events. Emscripten proxies the native entry
to a pthread, transfers `#canvas` with OffscreenCanvas, and enables Asyncify for
SDL WebGPU's synchronous native waits over asynchronous browser operations.
Both the JIT and OPFS native calls run off the browser main thread. The
application unmounts OPFS on its worker after returning from setup or native
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

GitHub Pages cannot set response headers itself. Lucent's service worker adds
COOP/COEP to the exact cached application resources. The first navigation
registers it and reloads once; a persistent inability to isolate produces an
explicit refusal. A real browser test on a server without those headers reached
`crossOriginIsolated=true`, then loaded again with the server stopped. No
browser-support claim follows until the complete title works under this policy.

The browser entry point uses page keyboard and touch events rather than the
desktop loopback HTTP inspector; a browser worker cannot bind that socket.
The asset-free package has first-run ZIP refusal and storage/isolation checks,
while complete-install import and gameplay remain unqualified until a player
supplies the original files locally.

### W4 — Local install, storage and offline use

`web/app.mjs` owns the title page, file chooser and lifetime lock; Lucent owns
bounded Blob-to-OPFS staging. The application lock prevents another tab from
replacing an installation while native workers use it. A fresh page releases
only its known abandoned staging leaf before accepting another file.

`src/web/web_main.cpp` mounts OPFS and delegates to the native install picker,
archive transaction and exact title validator. The previous valid installation
survives a failed replacement. Lucent's ZIP owner must use bounded reads and
streaming decompression: mmap or copying a multi-gigabyte archive into wasm
linear memory is not an acceptable implementation.

Persistence grants are requested and their refusal is visible. OPFS availability
is not a guarantee against eviction. Acceptance includes a real complete-install
import, invalid/corrupt input refusal, preservation of an existing install,
progress during import, saves/settings across reload, and offline relaunch of the
installed app. Merely caching the launcher is not offline game evidence.

### W5 — Floating-point semantics

WebAssembly has no host floating-point environment matching x87. The shared
software x87 path supplies rounding and exception semantics; the pinned software
suite passed 3,140 checks. Emitting an x87 guest block through WASM is a separate
W1 contract and must be tested through actual published modules.

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
