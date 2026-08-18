# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A static-recompilation port of **X-Men Legends II: Rise of Apocalypse** (2005 PC
build) to native C. The x86-32 machine code of `XMen2.exe` + the `libIG*.dll`
Alchemy engine DLLs is translated mechanically to C, so the game runs early; then
subsystems are replaced with hand-written native code while the rest keeps
working. `README.md` states the goal and the three shipped features;
`docs/strategy.md` states why recomp and why the PC build (the Xbox path in
`xbox/` is real, kept, but not the live front).

## Start here, before touching anything

Three in-repo registries answer one question each. Consult them at the START of a
task; update them in the SAME commit that changes a subsystem.

```sh
python3 tools/info.py brief <words>     # claims (what's proven + its falsifier) + instruments (which tools can be trusted)
python3 tools/re_frontier.py next       # the next RE-ready step;  `hacks` = the debt list
python3 ~/.claude/skills/issue-catalog/catalog.py search <symptom>   # docs/issues/ — bugs and dead ends already hit
```

- `docs/codemap.md` — what exists, where, and its honest status per subsystem.
- `docs/info/claims/` — each claim carries the observation that would falsify it.
  `info.py claim check` detects rot mechanically (has the cited code changed?).
- `docs/info/instruments/` — a tool that lied is recorded here. Several have.
- `docs/RE/` — reverse-engineering write-ups (`ark.md`, `enbaya_decode.md`,
  `conversations.md`). These are written to stand ALONE, for anyone reading the
  game rather than this port — keep them that way when a port detail changes.
- `docs/prior-art.md` — **Dusklight**, a shipping TP port of the same shape and
  CC0. Read it BEFORE designing any subsystem a mature port has already solved
  (interpolation, UI, config, mods, input binding, saves). Cite what you take,
  in the file that takes it.

## Setup

Copy `.env.example` → `.env` (gitignored) and fill in `GAME_PC_DIR`, `XBOX_ISO`,
`WINE_PREFIX`. Every script sources `.env`; **no machine-specific path may ever
appear in a tracked file**. Game assets are never committed and never modified —
run directories are symlink farms over the install.

All run artifacts go to the gitignored `scratch/`, structured by type
(`scratch/logs/`, `screenshots/`, `recomp/`, `run/`, `build-*/`). Never `/tmp`.

## Build and run

**Asset viewers + unit tests** (`build/`, root `CMakeLists.txt`):

```sh
cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
ctest --test-dir build -R controller          # one test
```

**Native (Wine-free) build — `x2native`**, the live front. Needs the generated
recompiler output to exist first (it is gitignored):

```sh
tools/ghidra_export.sh <module>                              # PE -> scratch/recomp/<module>.json
python3 tools/recomp.py emit   scratch/recomp/<m>.json src/recomp/<m>.c --split 1500
python3 tools/recomp.py native scratch/recomp/<m>.json src/recomp/<m>_native.c
cmake -S . -B scratch/build-native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build scratch/build-native --target x2native -j$(nproc)
scratch/build-native/x2native --no-window --selftest   # postcondition battery; exit 77 = SKIP (no GAME_PC_DIR)
tools/smoke_loop.sh                                    # END-TO-END: one run all the way round (~300 s)
tools/smoke_loop.sh --selftest                         # its checks, proved able to fail; needs no game
scratch/build-native/x2native --no-window --run        # module init + the exe's CRT startup, NO renderer
scratch/build-native/x2native --d3d8                   # the LIVE path: arms the host Direct3D 8, and implies --run
./run.sh                                               # the same --run, on YOUR screen, building first if needed
```

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
./run.sh wine                              # on YOUR screen with sound — for looking
./run.sh stock                             # the untouched install, as the control
```

`./run.sh` defaults to the **native** build; the Wine paths keep their own
names. `stock` is the control every rendering question is settled against and
`wine` is still the only configuration that draws the game, so neither is
going away while the renderer is unfinished.

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

Guest x86 → C, run inside a 64-bit ELF host:

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

## Rules this codebase enforces on itself

These are not style preferences — each one exists because its absence produced a
logged defect.

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
  same binaries. Kill by PID (`~/.claude/skills/safe-kill`).
