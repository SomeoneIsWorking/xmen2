# AGENTS.md

Guidance for any coding agent working in this repository. It is deliberately
agent-agnostic: there is ONE such document, and `CLAUDE.md` is a pointer to it.
Two copies drifted apart within a fortnight once, which is the same failure the
shared-tooling split exists to end -- each was improved wherever it happened to
be read, and neither improvement reached the other.

## What this is

A native port of **X-Men Legends II: Rise of Apocalypse** (2005 PC build). The
x86-32 machine code of `XMen2.exe` + the `libIG*.dll` Alchemy engine DLLs is
executed at runtime by `shared/x86port`'s engine, read from the player's own
images, so the game runs from the start; then subsystems are replaced with
hand-written native code while the rest keeps working. Nothing is statically
recompiled to C -- that corpus and its generator were deleted 2026-09-02.
`docs/project-goals.md` owns the durable outcomes, `docs/project-state.md` owns
factual capability coverage, and `docs/strategy.md` states why the guest is run
at runtime and why the PC build (the Xbox path in `xbox/` is real, kept, but not
the live front).

## Start here, before touching anything

The project-information entry point queries every in-repo authority. Consult it
at the START of a task; update the owning authority in the SAME commit that
changes a subsystem.

```sh
uv run --frozen python tools/info.py brief <words>
```

The registry commands under `tools/` are SHIMS. Their implementations live in
the `re-harness` repo, shared with
every port in the tree (nine forked copies of `info.py` had drifted into seven
versions); the DATA they read stays here. `tools/shared_dir.py` is the one place
a shared repo is located, and it refuses naming every path it tried rather than
falling back to a vendored copy.

- `docs/project-goals.md` — epic outcomes, constraints and non-goals.
- `docs/project-state.md` — verified/partial/blocked/missing capability coverage
  and the one current focus.
- `docs/codemap.md` — subsystem ownership and placement only.
- `docs/issues/` — atomic tasks, bugs, blockers, findings and dead ends.
- `docs/re-frontier.md` — the ordered binary/asset-grounded RE dependency chain.
- `docs/info/claims/` — each claim carries the observation that would falsify it.
  `info.py claim check` detects rot mechanically (has the cited code changed?).
- `docs/info/instruments/` — a tool that lied is recorded here. Several have.
- `docs/RE/` — port-specific reverse-engineering write-ups. `shared/alchemy`
  already owns partial native format/image/mesh/raster/Enbaya and input
  foundations; this port provisions some of its XMLB/ARK tooling but does not
  link or call a shared gameplay engine. Keep title evidence here until an
  X-Men 2 shipping-path integration proves which contract is reusable.
- `docs/prior-art.md` — provenance and historical context for code, assets, or
  vocabulary already adapted from external projects, including CC0 Dusklight.
  It is not an architecture authority or a design checklist. Preserve exact
  attribution for material actually reused; derive current ownership from this
  repository's codemap, contracts, and shipping-path evidence.

## Setup

For the default product, put the matching PC install at `./game/` or set
`GAME_PC_DIR` in `.env` (gitignored), then use `./run.sh`. `XBOX_ISO` and
`WINE_PREFIX` are needed only by their purpose-specific RE/oracle tools. No
machine-specific path may ever appear in a tracked file. Game assets are never
committed or modified.

The Linux AppImage is a desktop-first path: it does not require a terminal or
`GAME_PC_DIR`. Its first launch passes `--appimage` to the native runner, which
shows a setup prompt with Browse, validates the selected `XMen2.exe` or a ZIP
containing exactly one copy at any depth, and remembers the resulting install
under the OS user configuration directory. The AppImage contains no game
files. The Android APK has a separate setup Activity: it uses SAF to stage a
ZIP or an install folder into app-private storage, validates the loader and
title content sentinels, then starts SDL only after the native bridge has
supplied Lucent's Android user-data root. Its touch
events feed the same virtual DirectInput pad as every other controller path.
The remaining mobile release gate is measured device performance; see
`docs/android-release.md`.

Transient run artifacts go to the gitignored `scratch/`, structured by type
(`scratch/logs/`, `screenshots/`, `raw/`, `run/`). Compiler outputs, generated
redistributable assets, dependencies, and packages live under top-level
`build/`. Never `/tmp`.

## Build and run

**There is ONE native build directory: `build/native/`.** `./run.sh` creates
and maintains it, and it is the tree every live harness runs
(`tools/live_case.py` and `tools/x2ctl.py` among them).
It holds the asset viewers, every unit test and `x2native` itself.

A second tree configured by hand (`cmake -S . -B build`) used to exist and is
DELETED. It emitted a target of the same name that no harness ever ran, so
building it produced a binary that looked current and was not — a full live
PASS report once described an hour-old binary (issue #118) — and its bare
configure picked up the system interpreter instead of the locked `.venv`, so
`pad_font` failed on a missing Pillow. Do not recreate it.

```sh
./run.sh                         # provision, build and launch the one default product
cmake --build build/native -j$(nproc)          # build without launching
ctest --test-dir build/native --output-on-failure
ctest --test-dir build/native -R controller    # one test
uv run --frozen python tools/provision.py  # provision-only maintainer/cold-path check
build/native/x2native --no-window --selftest   # postcondition battery; exit 77 = SKIP (no GAME_PC_DIR)
build/native/x2native --no-window --run        # module init + the exe's CRT startup, NO renderer
build/native/x2native --d3d8                   # the LIVE path: arms the host Direct3D 8, and implies --run
```

`run.sh` takes no arguments and delegates directly to the locked Python
initializer. It validates the user's exact PE images, provisions pinned
redistributable dependencies and native assets, builds, and launches the one
native-overrides + x86port-JIT gameplay product. It must never invoke an
offline guest translator or reconstruct a guest-code corpus. Ghidra is a
maintainer-only analysis tool. Diagnostics, provisioning-only checks and the
independent Wine control remain separate tools rather than launcher commands.

The product has no execution-engine selector. The interpreter belongs only in
a separately built x86port test/diagnostic target and must be absent from the
gameplay target's link closure as well as its configuration, environment, and
command-line surfaces. Until the implementation and link audit establish that
boundary, treat it as open migration work recorded in `docs/project-state.md`,
not as permission to expose `engine=interpreter` in a player build.

Build a Linux AppImage from the verified native build with
`uv run --frozen python tools/package_appimage.py`. The packager stages only
the native binary, UI resources, desktop metadata, and libraries discovered by
`linuxdeploy`. It injects a `patchelf` 0.19+ binary into the deployer's
temporary AppImage payload, because older patchers corrupt `DT_INIT` in current
Fedora ELFs; `appimagetool` writes the result to
`build/release/X-Men-Legends-II-x86_64.AppImage`.

Build the ARM64 APK from a selected Android SDK/NDK with
`uv run --frozen python tools/build_android.py`. The dependency step consumes
the pinned `shared/android-port` prefix under `build/deps/android/`; Android
CMake never consults host `pkg-config` or fetches title-local SDL/FFmpeg
sources. The Gradle project consumes the generated
`x2-android.properties` contract and stages only native code, UI resources,
and SDL's Java shell, never game files.

**Drive a run instead of scripting it.** The default product opens an HTTP
channel on loopback, records the exact post-merge DirectInput states returned to
the game, and publishes its PID, port and recording in `scratch/run/live.json`.
Commands are applied on the guest's own input poll, never from the server
thread:

```sh
./run.sh
tools/x2ctl.py probe                  # status + input + frame when capturable
tools/x2ctl.py status                 # frames, guest time, frame timing
tools/x2ctl.py key Return --hold 0.4  # press keys, in order
tools/x2ctl.py shot scratch/screenshots/now.png
tools/x2ctl.py watch --for 30         # /status once a second
tools/x2ctl.py recording --events 20  # tail the automatic JSONL trace
```

Every refusal is an answer: 409 says the key has no DirectInput mapping or the
run has no frame to capture, 504 says the guest never polled -- which is a fact
about the run, not a transport failure. `--unbounded` skips the scheduler's
idle waits; `X2_UNPACED=1` removes the game's own frame cap; `X2_BOOT_MAP=<map>`
starts in a level instead of through the menus while still running the retail
`startFirstMission` party initializer.

JIT diagnostics must report translated blocks, native hand-backs, refusals,
and denominators while the product runs. Runtime reachability replaces the old
static function-discovery loop; no tool may seed, emit, compile, or stage guest
code for a product build.

**Wine oracle.** The unmodified PC release under Wine remains an independent
behavioral control. It may be instrumented to observe the retail boundary, but
no generated or replacement guest-code DLL is a product mode or a retained
static oracle. Do not regenerate, build, or run the retired static product.

**Ask the control for independent evidence, not only a similar-looking
picture.** The retained stock-oracle path currently owns cached driven frames
and proxy observations. A CPU, memory, timing, or device comparison is not
available merely because an older probe once existed; add a bounded instrument
with image identity, denominators, and both-answer controls before making that
claim. Bisecting frames by eye is not representative conformance evidence.

**Never run the control twice for the same question — go through the cache.**
A driven `stock` run is five to nine minutes of Xvfb, Wine and a software
rasteriser, and it produces the same frames every time:

```sh
X2_KEYS="195-300/12:Return,380-500/20:Return" X2_SAMPLES=6 \
  uv run --frozen python tools/oracle.py run stock 540
uv run --frozen python tools/oracle.py list
```

The key covers the driving script, the duration, the sample count **and a
fingerprint of the run directory**, so a rebuilt DLL misses. Every capture is
kept with its brightness already measured (`mean_luma`, `frac_lt16`,
`frac_gt128`), so re-asking about the pixels costs nothing. A hit says it is a
hit and how old it is; a cached frame must never read as a fresh observation.

`X2_WRITE_WATCH=<guest-address>` and the in-process crash reporter provide live
runtime evidence without manufacturing a static product. Use those instead of
gdb/winedbg, both of which produced nothing usable here (issue #1).

The Xbox material is a source of independently recovered behavioral facts such
as controller defaults, not a second product. Do not extend, generate, build,
or run its static-recompiler path. Preserve still-useful observations in the
claims/RE authorities and remove obsolete Xbox product machinery when that
evidence has been consolidated.

## Architecture

Guest x86-32 is read from the user's authenticated PE images and translated on
demand into host instructions by `shared/x86port`'s JIT:

- **`src/native/guest_modules.c` + `pe_map.c`** discover the required images at
  runtime, map/relocate them, and bind their IAT slots. Product provisioning
  supplies no precomputed function map or generated guest body.
- **`shared/x86port`** owns x86 decode, semantics, host-code emission, and its
  runtime block cache. This repository pins and consumes the canonical shared
  implementation; title-specific CPU semantics do not belong here.
- **`src/native/x86_engine*.c` + `x86_dispatch.c`** compose bounded JIT runs,
  thread/call context, native hand-back predicates, import thunks, diagnostics,
  and scoped calls to an override's original guest body.
- **`src/native/`** owns title-specific native overrides and host services.
  Overrides are keyed by module identity plus linked address because the
  `libIG*.dll` images reuse linked bases. `guest_heap.c` provides the guest's
  32-bit-addressable arena; the DLL-named owners implement the Win32/CRT/SDL
  boundaries.
- **Test-only interpretation** belongs in an independently linked x86port test
  target. The gameplay binary neither links it nor selects it.
- **The shared Alchemy engine is a partial foundation, not a current gameplay
  dependency.** `shared/alchemy` already builds the `alchemy`, `alchemy_input`,
  and optional `alchemy_input_sdl` libraries, and this port provisions its
  XMLB/ARK tools. Today `x2native` links none of those libraries and includes or
  calls no shared runtime API: retained engine behavior executes through the
  JIT and native replacements remain in the title-owned codemap rows. X-Men 2
  must establish the first gameplay integration and conformance proof; the
  first candidate is the `alchemy_input` guest `igControllerManager` adapter
  specified by `shared/alchemy/docs/input.md`. Do not start MUA engine migration
  until every X-Men 2 project goal is verified; then migrate MUA to the proven
  shared boundary without rewriting its gameplay.

### Project-owned host composition

The host is governed by this repository's cohesive-owner boundaries and
`docs/codemap.md`. `src/config/` owns persistent data and storage location;
`src/presentation/` owns window-mode transitions; `src/input/` resolves player
assignments and publishes them into guest binding sets; `src/ui/` owns only the
RmlUi lifetime and documents. `win32_sdl.c` and `gpu_device.c` compose those
owners at the SDL event and render boundaries; they do not absorb their policy.
New behavior goes to the smallest existing owner, or establishes a narrow new
owner and updates the codemap in the same change. External projects may provide
attributed provenance, but never substitute for a local contract or regression.

Save paths keep the same split: `shell32.c` owns the writable profile root used
by config and registry storage, while `src/save/save_directory.{c,h}` owns the
one title-specific retail leaf directory below it. Catalog, Continue, boot and
autosave consume that authority; none rebuilds `Activision/X-Men Legends 2/Save`.
`src/config/config_directory.{c,h}` resolves and creates the OS user
configuration directory. It is also the persistence owner for the AppImage
install selection; `X2_SAVE_DIR` remains an explicit portable/diagnostic
override.

The AppImage setup boundary is locally owned: SDL3 dialog/file-picker mechanics
live in `src/native/install_picker.cpp`, resource location lives in
`src/ui/ui_resources.cpp`, and release staging lives in
`tools/package_appimage.py` plus `packaging/`. `x2native.c` only composes the
setup result into the existing asset mapping path.

The Android setup boundary follows the same pattern: `android/` owns Activity
lifecycle, SAF URI permissions, and app-private staging;
`src/native/android_bridge.cpp` only transfers the absolute storage/source
contract; `install_picker.cpp` owns title validation and shared Lucent ZIP
extraction. The Android touch boundary keeps the title's safe-area-aware action
vocabulary and virtual layout in `src/input/touch_controls.cpp`; platform
SDL/Activity event acquisition, visual feedback, and guest input publication
remain outside that owner. `lucent::touch::Router` owns contact capture,
multi-touch, and cancellation. `src/presentation/touch_hud_layout.c` owns the
pure edge-relocation policy and `src/native/touch_hud_runtime.c` scopes it
around the retained CHud bodies; portrait taps re-enter the existing retail
mouse handler.

Boot selection follows the same boundary: `src/config/boot_mode.{c,h}` owns the
persistent vocabulary, `src/native/boot_mode_policy.{c,h}` owns the pure
Normal/Menu/Continue decision, `boot_mode_runtime.{c,h}` owns the one boot
request and latest-save leaf, `boot_menu_transition.{c,h}` owns the exact
retail main-menu call, and `startup.c` only composes those owners.

Exact input capture belongs to `src/input/input_record.{c,h}` and runs only
after physical, scripted, control-channel and modal-UI policy produce the state
the guest will receive. `src/native/live_session.{c,h}` owns live-run discovery;
`x2native.c` only composes those owners.

In-game cutscene skipping belongs to `src/native/cutscene_player.{c,h}`. It
owns the control-lock epoch, composes exact steps from the ported BehavEd
player (`behaved_player`) and title timed-event player
(`cutscene_event_player`), and treats `conversation_player` as a deterministic
payload adapter only. `cutscene_dialogue` owns the synchronous player's scoped
suppression of the exact retail response-voice and line-voice presenters and
cancels the current handle before advancing. It also composes the generic
`audio_play_policy` scope, which refuses every new DirectSound start caused by
synchronous cutscene work without pausing existing ambient/gameplay voices.
Ordinary event/fiber pumps retain retail deadline rules; the synchronous skip
never runs a world update or changes the guest clock.
See `docs/RE/cutscene_player.md`.

Native SFD playback follows the same boundary. `src/media/fmv_player.{c,h}`
owns FFmpeg demux, MPEG-1 video decode and timestamp policy;
`fmv_audio_decode.{c,h}` owns ADX receive/resampling, while
`fmv_decoder_drain.{c,h}` owns the shared flush/backpressure/EOF contract.
`src/audio/movie_audio.{c,h}` owns the single streaming voice mixed by
DirectSound. `src/native/movie.c`
only bridges the evidenced `igCriMovieCodec` methods and writes the guest
runtime `igImage`, leaving libMovie's scene, texture upload, lifetime, and
callback behavior intact. See `docs/RE/fmv.md`; no media asset belongs in git.
`src/media/fmv_probe.{c,h}` owns opt-in decoded-to-padded-to-upload row
verification; `src/d3d8/d3d8_texture_luma.{c,h}` owns the independent texture
luma diagnostic rather than growing the D3D8 resource implementation.

Shadow ownership follows the title evidence in `docs/RE/shadows.md`.
`CShadowMgr` owns per-entity procedural floor-decal selection and the GPU renders
the resulting ordinary scene packets. `DetailedShadow` is dead persisted
configuration, not a renderer switch or an exposed retail control. The separate
native enhancement keeps generic packet classification/matrices in
`src/gpu/shadow_policy`, sampleable resources/pass lifetime in `gpu_shadow`, and
its Video toggle in settings/RmlUi. It reconstructs skinned world positions from
the exact VS output and runs before aspect-fit composition and RmlUi. It does not
claim the title's monster-spawner `shadow` or power-effect `no_shadow` metadata
is authored caster policy; that future seam belongs at Alchemy scene traversal.

The shipped settings UI uses the exact pinned RmlUi revision and maintained
SDL3/SDL_GPU backends named in `CMakeLists.txt`. Its document vocabulary and
RCSS are adapted from Dusklight's CC0 `res/rml/window.rcss` at commit
`0fc05028ccfe809c569b1b84c0bb87f382b0bf34`. Dusklight's compositor-only blur
and shadows are replaced by an opaque/dimmed fallback because the SDL_GPU
backend renders those effects incorrectly. Keyboard mappings belong to four
reusable profiles; players reference profiles, while controllers are assigned
by persistent identity and always use the canonical Xbox/PS2 layout.
This paragraph records source provenance only; the local UI/config/input owners
and their tests define the current architecture.

`tools/check_structure.py` is the normal mechanical boundary: new host source
files are capped at 500 lines and existing larger files are frozen. Extract a
cohesive owner and lower a legacy limit; never raise one to land a feature.

## Required repository guardrails

These are implementation and verification requirements, not optional style
preferences. Any guardrail not yet wired into the normal verifier remains
migration work; documentation does not make it verified.

- **Formatting and linting are release gates.** Python automation is checked by
  Ruff. First-party C/C++ uses a tracked `.clang-format`, and `clang-tidy` runs
  against the real compile commands; both non-mutating checks belong in the
  normal verifier. Format touched sources and fix diagnostics at their cause.
  Do not preserve the former formatter exemption, blanket-suppress findings, or
  raise a structure limit merely to land a change.

- **The JIT refuses unsupported execution by exact cause.** A missing decoder,
  instruction semantic, host backend, executable image, import, or native
  hand-back is a named failure. There is no best-effort no-op, offline-emitted
  substitute, interpreter fallback, or smaller product that looks like
  progress.
- **Product configuration cannot choose the execution architecture.** The
  gameplay target always uses the x86port JIT. `lucent::cvar` owns layered
  optional diagnostics and title tuning; it must not expose an interpreter,
  static substrate, fallback, or required product component as a mutable CVar.
  Test-oracle controls belong to a separately built test target.
- **Use one project logger.** Route configurable diagnostics through Lucent's
  logger, one call per site, without `if (debug) fprintf(...)` wrappers or new
  ad-hoc print gates. Fatal boundary refusals may terminate directly, but a new
  subsystem does not invent another logging policy.
- **Keep execution ownership cohesive.** x86 decode, semantics, host emission,
  and block-cache policy belong in `shared/x86port`; title identity, native
  overrides, imports, and game policy belong here. The host entry point only
  composes them. New source files stay below 500 lines; split an already mixed
  or oversized owner before extending it, and never create a generic runtime,
  manager, or override bucket.
- **A negative result must carry its denominator and its blind spots.** "Found
  nothing" and "never looked" must be distinguishable. A product-link audit,
  backend probe, or gameplay trace that inspected zero candidates must refuse.
- **An override must reproduce the original's RETURN VALUE, not just its stack
  effect.** Check the CALL SITE, not the decompiler's signature: Ghidra typed
  the DirectX check `void`, the caller does `TEST AL,AL` on it, and an override
  that left EAX alone made the game branch on leftover register contents --
  a silent `exit(0)` before the first frame, intermittent, and sensitive to
  anything that changed what ran before it (issue #54, C158).
- **A counter that only prints at shutdown cannot measure this program.**
  Nothing here stops on its own: every run ends in a timeout, and the shutdown
  report is written from a signal handler that can be cut short. Three counters
  in one session were unreadable for exactly that reason. Put a live number in
  the HEARTBEAT, and print it AT ZERO with its denominator -- "0 of 352,340" is
  a measurement, a line that appears only when something is wrong is
  indistinguishable from a check that never ran.
- **Diagnostics prove they fire.** `--selftest` / `*_SELFTEST=1` paths exist for
  the watch, the crash reporter, the indirect-call checks; they are wired into
  the test suite. An instrument caught lying is recorded in `docs/info/instruments/`
  and every result depending on it is re-checked.
- **`⛔ hack` in `re_frontier.py` is debt, never a resting state**, and
  `re-verified` means the output matches the real game on real data — an internal
  trace ("the call site was reached") is a mechanism check, not faithfulness.
- **Never `pkill -f` a shared binary name** — several agents and the user run the
  same binaries. Kill by PID through the `safe-kill` skill.
- **Code lives where it belongs in a game, not in a bucket.** The hand-written
  host code is organized by subsystem ownership, mirroring where the code would
  live in a native game: a native override's implementation goes in the file
  named for its game-code subsystem (`startup.c` for boot/run-control,
  `movie.c` for the media decoder, `reportbox.c` for the error dialog,
  `conversation.c` for the conversation manager, the `dinput_*`/`pad_glyphs.c`/
  `xbox_defaults.c` files for input, `prompt_labels.c` for prompt composition,
  `prompt_glyph_batch.c` for the Alchemy text-batch seam, and
  `gpu_prompt_glyphs.c` for prompt pixels), NOT in a
  central `overrides.c` (abolished 2026-08-16). An override declares itself
  where it lives, with `x86_register_override("<module>.dll", 0x…, fn)` beside
  its implementation; runtime dispatch routes calls to the registered entry
  point and `x86_guest_body` scopes a call to the original through the JIT.
  **The module name is not decoration**: every `libIG*.dll` is
  linked for 0x10000000, so a bare address matched whichever module happened to
  land there -- two overrides were dead and one could fire for the wrong module
  (C212). Same rule for everything else: a new subsystem gets its own file, and
  adding to an existing one means finding the file that owns it first.
- **An agent must be able to DRIVE a run, not just launch it.** `--control`
  opens a loopback HTTP channel (`tools/x2ctl.py`) that presses keys, reads
  where the game is and captures the frame WHILE it runs. Reach for it instead
  of writing another fixed input script.
- **A play-through is an OBSERVATION, never a gate.** Input scripts are
  scheduled by frame and fire whether or not the game reached the state they
  were written for, so a drifted run spends every press in the menus, draws a
  plausible picture and "passes". Gate on unit tests, runtime invariants and
  counters with denominators. Before reading the ABSENCE of a symptom as a fix,
  prove the run reached the code at all -- and prefer `X2_BOOT_MAP` over
  driving the menus.
- **Automated and agent-driven runs are silent and fast by default.** Use the
  timed silent device for `--no-window`, or SDL's dummy audio backend when a
  real window is required for presentation/capture verification. Pass
  `--unbounded` and `X2_UNPACED=1` unless real-time pacing is the subject of the
  test. Never open the host playback device or run paced just because a test
  needs a window. Where silence would change behaviour -- the game advances
  cutscenes off DirectSound play cursors -- preserve advancing play cursors;
  do not merely disable audio (`dsound.c`).
- **One clock the guest can see** (`guest_clock.c`). Five files had private
  `now_s()` copies reading CLOCK_MONOTONIC; the guest gates real logic on
  elapsed time, so two of them disagreeing is a timing bug wearing a gameplay
  bug's clothes. `--unbounded` skips the scheduler's idle waits -- only
  intervals in which no thread was runnable, never scaled time.
