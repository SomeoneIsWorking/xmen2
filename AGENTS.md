# AGENTS.md

Guidance for any coding agent working in this repository. It is deliberately
agent-agnostic: there is ONE such document, and `CLAUDE.md` is a pointer to it.
Two copies drifted apart within a fortnight once, which is the same failure the
shared-tooling split exists to end -- each was improved wherever it happened to
be read, and neither improvement reached the other.

## What this is

A static-recompilation port of **X-Men Legends II: Rise of Apocalypse** (2005 PC
build) to native C. The x86-32 machine code of `XMen2.exe` + the `libIG*.dll`
Alchemy engine DLLs is translated mechanically to C, so the game runs early; then
subsystems are replaced with hand-written native code while the rest keeps
working. `docs/project-goals.md` owns the durable outcomes,
`docs/project-state.md` owns factual capability coverage, and
`docs/strategy.md` states why recomp and why the PC build (the Xbox path in
`xbox/` is real, kept, but not the live front).

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
- `docs/RE/` — port-specific reverse-engineering write-ups (currently
  `boot.md`). Shared Alchemy write-ups live in the `alchemy` repository under
  `docs/` (`ark.md`, `enbaya_decode.md`, `conversations.md`); keep them there so
  both games consume one authority.
- `docs/prior-art.md` — **Dusklight**, a shipping TP port of the same shape and
  CC0. Read it BEFORE designing any subsystem a mature port has already solved
  (interpolation, UI, config, mods, input binding, saves). Cite what you take,
  in the file that takes it.

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
files. The Android APK shell is not implemented here yet; its setup, touch
layout, and performance evidence contract live in `docs/android-release.md`.

All run artifacts go to the gitignored `scratch/`, structured by type
(`scratch/logs/`, `screenshots/`, `recomp/`, `run/`, `build-*/`). Never `/tmp`.

## Build and run

**There is ONE build directory: `scratch/build-native/`.** `./run.sh` creates
and maintains it, and it is the tree every live harness runs
(`tools/live_case.py`, `tools/native_discover.sh`, `tools/x2ctl.py`'s target).
It holds the asset viewers, every unit test and `x2native` itself.

A second tree configured by hand (`cmake -S . -B build`) used to exist and is
DELETED. It emitted a target of the same name that no harness ever ran, so
building it produced a binary that looked current and was not — a full live
PASS report once described an hour-old binary (issue #118) — and its bare
configure picked up the system interpreter instead of the locked `.venv`, so
`pad_font` failed on a missing Pillow. Do not recreate it.

```sh
./run.sh                         # provision, build and launch the one default product
cmake --build scratch/build-native -j$(nproc)          # build without launching
ctest --test-dir scratch/build-native --output-on-failure
ctest --test-dir scratch/build-native -R controller    # one test
uv run --frozen python tools/provision.py  # provision-only maintainer/cold-path check
scratch/build-native/x2native --no-window --selftest   # postcondition battery; exit 77 = SKIP (no GAME_PC_DIR)
scratch/build-native/x2native --no-window --run        # module init + the exe's CRT startup, NO renderer
scratch/build-native/x2native --d3d8                   # the LIVE path: arms the host Direct3D 8, and implies --run
```

`run.sh` takes no arguments and delegates directly to the locked Python
initializer. It reconstructs all twenty gitignored recompiler outputs from the
committed encoding-free exports plus the user's exact PE images; Ghidra is a
maintainer-only discovery tool. Diagnostics, provisioning-only checks and Wine
controls remain separate tools rather than launcher commands.

Build a Linux AppImage from the verified native build with
`uv run --frozen python tools/package_appimage.py`. The packager stages only
the native binary, UI resources, desktop metadata, and libraries discovered by
`linuxdeploy`; `appimagetool` writes the result to
`scratch/release/X-Men-Legends-II-x86_64.AppImage`.

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

Configure `-DX2_NATIVE_TRACE=ON` to trace every recompiled body into the
boundary ring. CMake *says* which modules it linked and which it skipped — a
missing module is announced, never silently omitted.

**Discovery loop** — static analysis misses constructor-table targets (referenced
only by a data pointer). This feeds the runtime's missing-target report back into
Ghidra as seeds and rebuilds until a round finds nothing:

```sh
tools/native_discover.sh [max-rounds]
```

**Wine oracle / hybrid DLL path** (the original game with one recompiled DLL
swapped in; still the reference for "does it render"):

```sh
tools/build_recomp.sh ALL libIGDisplay     # emit+runtime+dll+compile+stage
tools/run_shim.sh recomp 30                # HEADLESS, Xvfb, screenshot — for measuring
```

The Wine paths are measurement tools, not alternate modes of `run.sh`. `stock`
remains the control every rendering question is settled against.

**Ask the control for DATA, not only pixels.** `tools/oracle_probe.py --pid <pid>`
samples a live run's guest state from outside (`process_vm_readv`, no debugger,
no perturbation) at the same fields the port reports about itself, so a
port-vs-control comparison is a diff of two number streams instead of two
screenshots. It refuses to guess which process to read and verifies the image
before sampling. Bisecting frames by eye is the thing it replaces.

**Never run the control twice for the same question — go through the cache.**
A driven `stock` run is five to nine minutes of Xvfb, Wine and a software
rasteriser, and it produces the same frames every time:

```sh
X2_KEYS="195-300/12:Return,380-500/20:Return" X2_SAMPLES=6 \
  python3 tools/oracle.py run stock 540      # runs on a miss, replays on a hit
python3 tools/oracle.py list                 # what is already answered
```

The key covers the driving script, the duration, the sample count **and a
fingerprint of the run directory**, so a rebuilt DLL misses. Every capture is
kept with its brightness already measured (`mean_luma`, `frac_lt16`,
`frac_gt128`), so re-asking about the pixels costs nothing. A hit says it is a
hit and how old it is; a cached frame must never read as a fresh observation.

`WATCH=1 tools/build_recomp.sh …` adds the entry-point watch (`X2_WATCH=0x…`,
writes to a file — the game is a GUI-subsystem process with no stderr) and the
in-process crash reporter. Use those instead of gdb/winedbg, both of which
produce nothing usable here (issue #1).

**Xbox path** (`xbox/`, vendored toolkit in gitignored `vendor/xboxrecomp` —
changes live as patches in `patches/`): `tools/xbox_relift.sh`,
`tools/xbox_run.sh`, `tools/xbox_discover.sh`, `tools/check_patches.sh`.

## Architecture

Guest x86 → C, run inside a 64-bit ELF or Mach-O host:

- **`tools/ghidra_scripts/ExportFuncs.py`** (+ `SeedPointerTables.py`,
  `MergeTruncated.py`, `SplitFunction.py`) — Ghidra does the genuinely hard part:
  recursive-descent boundary discovery and code/data separation. Output is JSON
  per module in `scratch/recomp/`.
- **`tools/recomp.py`** — the translator, not an analyser. Subcommands:
  `report` · `emit` (bodies) · `runtime` / `native` (dispatch table) · `dll`
  (export shims + import thunks for the Wine path).
- **`src/recomp/*.c`** — **generated, gitignored, NEVER hand-edited.** One C
  function per guest function over a CPU-state struct. `src/recomp/x86rt.h` is
  the hand-written runtime header (registers, lazy flags, dispatch).
- **`src/native/`** — the host: `pe_map.c` maps and relocates the real PE images
  and binds every IAT slot as a loader would; `x86rt_native.c` dispatches across
  modules keyed on **mapped** address (every `libIG*.dll` is linked for
  `0x10000000`, so linked addresses collide); `guest_heap.c` serves the guest a
  32-bit-addressable arena; `kernel32.c` / `crt.c` / `win32_sdl.c` implement the
  imports on POSIX/SDL3.
- **The Alchemy engine layer is NOT in this repo.** The IGB asset readers, the
  XMLB container, the ARK tooling and the `ig` controller abstraction belong to
  the engine this game shares with Marvel Ultimate Alliance -- which is a 360,
  big-endian title -- so they live in the `alchemy` repo, consumed rather than
  vendored. Its viewers (`x2view`, `meshview`, `flyview`), `igb_dump` and its
  three tests build from there. `tools/alchemy_path.py` is the ONE place this
  port resolves that checkout, and it refuses rather than guessing when it is
  missing. `src/app/` keeps only `x2run.c`, which is this port's own runner.

### Dusklight host ownership applied here

The host follows Dusklight's composition pattern, adapted to this port's C ABI
and SDL_GPU renderer. `src/config/` owns persistent data and storage location;
`src/presentation/` owns window-mode transitions; `src/input/` resolves player
assignments and publishes them into guest binding sets; `src/ui/` owns only the
RmlUi lifetime and documents. `win32_sdl.c` and `gpu_device.c` compose those
owners at the SDL event and render boundaries; they do not absorb their policy.

Save paths keep the same split: `shell32.c` owns the writable profile root used
by config and registry storage, while `src/save/save_directory.{c,h}` owns the
one title-specific retail leaf directory below it. Catalog, Continue, boot and
autosave consume that authority; none rebuilds `Activision/X-Men Legends 2/Save`.
`src/config/config_directory.{c,h}` resolves and creates the OS user
configuration directory. It is also the persistence owner for the AppImage
install selection; `X2_SAVE_DIR` remains an explicit portable/diagnostic
override.

The AppImage setup boundary follows Dusklight's composition pattern: SDL3
dialog/file-picker mechanics live in `src/native/install_picker.cpp`, resource
location lives in `src/ui/ui_resources.cpp`, and release staging lives in
`tools/package_appimage.py` plus `packaging/`. `x2native.c` only composes the
setup result into the existing asset mapping path.

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

`tools/check_structure.py` is the normal mechanical boundary: new host source
files are capped at 500 lines and existing larger files are frozen. Extract a
cohesive owner and lower a legacy limit; never raise one to land a feature.

## Rules this codebase enforces on itself

These are not style preferences — each one exists because its absence produced a
logged defect.

- **Lint the Python, do not reformat the C.** `ctest python_lint` runs ruff over
  `tools/` and `tests/`; the rule selection in `ruff.toml` is defects, not
  preferences, and says what is deliberately left out and why. Ruff is not a
  build dependency, so `tools/lint.py` exits 77 (ctest SKIP) and names what did
  not happen rather than passing quietly when it is absent. There is no
  `.clang-format` on purpose: measured against the tree, the closest preset
  rewrites 56% of the lines it touches, and what it changes is damage — it
  flattens hand-laid-out tables like `DIK_MAP`, collapses aligned declarations
  and re-breaks the wrapped diagnostic strings. A formatter that cannot express
  the house style is not a tidying tool.

- **A translator that does not understand an instruction must fail loudly by
  name.** Never a comment, a no-op, or best-effort code. Unhandled cases raise
  `Unsupported`, the function is recorded untranslatable with the reason, and
  `recomp.py report` ranks the reasons by how many functions each blocks. A
  recompiler that quietly skips instructions produces a binary that runs and is
  wrong.
- **Every script refuses rather than producing something smaller that looks like
  progress**: a missing JSON, a zero-function export, a missing eps file, an
  export whose block layout disagrees with the shipped PE
  (`tools/verify_export.py`, wired into the discovery loop after issue #12).
- **A negative result must carry its denominator and its blind spots.** "Found
  nothing" and "never looked" must be distinguishable — see the shape of the
  reports in `native_discover.sh`.
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
  its implementation; the emitter scans `src/native/*.c` for those calls and
  routes every call to a registered entry point through the dispatcher's
  override slot. **The module name is not decoration**: every `libIG*.dll` is
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
