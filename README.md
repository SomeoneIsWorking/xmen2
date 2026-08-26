# X-Men Legends II — native PC port

Turn X-Men Legends II: Rise of Apocalypse (2005, Activision / Raven / Vicarious
Visions) into a **native, buildable codebase** by statically recompiling the PC
build's x86 machine code to C and then replacing subsystems with hand-written
native code. The current product runs without Wine: locally generated C executes
the code while the host maps the user's PC PE images for their data, relocation
layout and other non-code content.

**Direction: static recompilation of the PC build to native C, then native
overrides — see [`docs/strategy.md`](docs/strategy.md).** The whole binary is
translated mechanically so the game runs early, then subsystems are replaced
with hand-written C while the rest keeps working. The live native build links
the exe and every shipped engine module; unsupported instructions and missing
indirect targets refuse by name instead of silently falling back.

## What is in this repository — and what is not

**No game content is distributed here.** This repository contains the port's
source, its RE notes, and encoding-free analysis metadata needed to recreate the
native build. It ships no game executables, libraries, instruction bytes,
textures, audio, or data files. Every build restores restricted bytes from a
copy you already own, located through a gitignored `.env` (see `.env.example`).
The install directory is treated as strictly read-only; nothing is written back.

**You need your own legally obtained copy** of the 2005 PC release to build or
run anything. Without it the tools refuse rather than degrade: `--selftest`
exits 77 (SKIP) and says nothing was checked.

X-Men Legends II: Rise of Apocalypse is © Activision, developed by Raven
Software and Vicarious Visions. Marvel and X-Men are trademarks of Marvel
Characters, Inc. This project is unaffiliated with, and unendorsed by, any of
them. The generated C under `src/recomp/` is a mechanical translation of the
shipped machine code — it is derived from the original binaries, is gitignored,
and is produced on your machine from your copy. The MIT licence in
[`LICENSE`](LICENSE) covers **this repository's own code only** and makes no
claim over the game.

## Run from a fresh clone

The current native host supports Linux x86-64. macOS is blocked by its low
address-space reservation and Linux-specific host APIs; native Windows is not
implemented. The launcher refuses those platforms rather than implying that a
package install will fix the port.

Install `uv`, a C/C++ compiler (GCC or Clang), and the native development
packages. On Fedora/RHEL-family systems:

```sh
sudo dnf install SDL3-devel SDL3_image-devel ffmpeg-free-devel freetype-devel \
  glslc pkgconf-pkg-config vulkan-loader mesa-vulkan-drivers
```

On Debian 13 or Ubuntu 26.04 and newer:

```sh
sudo apt install libsdl3-dev libsdl3-image-dev libavformat-dev libavcodec-dev \
  libavutil-dev libswscale-dev libswresample-dev libfreetype-dev glslc \
  pkg-config libvulkan1 mesa-vulkan-drivers
```

Then place the matching PC release at `./game/`, or set `GAME_PC_DIR` in a
gitignored `.env`, and run:

```sh
./run.sh
```

That command has no modes or arguments. It enters the locked `uv` environment,
validates the game revision, fetches three pinned source dependencies into the
gitignored `vendor/shared/`, reconstructs generated C from committed metadata
and the user's PE files, builds, and launches the native game. Ghidra,
ImageMagick, a system Python environment, Wine, and sibling repository checkouts
are not player prerequisites. Maintainer and diagnostic entry points live under
`tools/`; they are deliberately not commands of `run.sh`. The `re-harness`
checkout is maintainer-only and is not fetched by the player bootstrap.

## Sources

- **PC build** (`$GAME_PC_DIR`): `XMen2.exe` (2.61 MB) + 16 `libIG*.dll` + `cgD3D8.dll` /
  `cg.dll` / `libMovie.dll` — **6.47 MB of x86 machine code total**. The gameplay is NOT in
  the binaries: it is data-driven (`Data/*.XMLB` = compressed XML, `Data/*.engb` = Enbaya,
  `Scripts/` Lua, `Conversations/`, `missions/`, `entities/`, `Maps/`).
- **Xbox ISO** (`$XBOX_ISO`): `default.xbe` (5.7 MB) plus its packages — the
  ground truth for the Xbox release's controller defaults and controller UI.
  No Xbox asset is shipped by this repository; prompt art is this port's SVG.
- **Alchemy 5.0 Kit** (archive.org `alchemy-kit_202309`, 152 MB): engine source + docs.
  The architectural Rosetta stone. XML2 ships Alchemy 3.2; 5.0 assets are version-
  incompatible but the engine architecture (igCore/igDisplay class model, IGB format,
  file-package system, DLL boundary) is directly analogous.

The durable outcomes are in
[`docs/project-goals.md`](docs/project-goals.md); factual capability coverage,
gaps and the current focus are in
[`docs/project-state.md`](docs/project-state.md).

## The three features (all land in the input layer)

1. **Controller hotswap** — implemented through SDL3 and the game's own
   DirectInput enumeration/connection callbacks; late attach and detach are
   exercised by a frame-scheduled virtual pad.
2. **Controller defaults UI** — implemented at the retained PC controller
   editor: Keyboard Defaults keeps the shipped keyboard table and Xbox Defaults
   applies the assignments recovered from the Xbox executable—not a modern
   mapping invented by the port. Black/White pack use remains a direct-action
   RE boundary because the PC binding table has no Health/Energy rows.
3. **Xbox button prompts** — the input-name overrides publish private prompt
   codepoints for the active controller or keyboard source, and the engine's
   own text layout positions them. The port renders shared SVG art from its own
   GPU atlas at the RE'd Alchemy text-batch boundary; the game's font pixels
   and UVs remain untouched. Only width, height, advance and baseline metrics
   for those private cells are published in memory. The 21-row bindable Xbox
   preset and native prompt draw path are implemented.

## Verification

- **Oracle**: the original PC build under Wine. `tools/run_shim.sh <rundir>` runs it
  headless on Xvfb and captures a frame; `scratch/run/stock` is the unmodified
  reference and `scratch/run/proxy` swaps in our DLL. Both are symlink farms — the
  real game install is never modified.
- **Ledgers**: `docs/info/` holds what has been *proven* (claims, each with the
  observation that would falsify it) and which tools can be *trusted* (instruments).
  Query with `info.py brief <words>` before re-deriving anything.

## Progress tracking

Current status has two maintained owners: [`docs/codemap.md`](docs/codemap.md)
says what exists and its honest coverage;
[`docs/re-frontier.md`](docs/re-frontier.md) orders the remaining RE work and
names shortcut debt. `python3 tools/re_frontier.py next` is the executable view.

## Reference materials (M1)

- **Alchemy 5.0 Kit** at `scratch/ref/alchemy5/Alchemy50/` (gitignored; re-download from
  archive.org `alchemy-kit_202309` if lost). Contents:
  - `include/` — **full engine header suite** (igCore/igDisplay/igSg/igGfx/...). Verified:
    `igController::BUTTONS`, `igControllerManager::initializeControllers` match the XML2
    `libIGDisplay.dll` binary exactly → Alchemy 3.2 class API ≡ 5.0 headers.
  - `DirectX9/lib/` — precompiled Alchemy 5.0 engine DLLs (reference behavior).
  - `sources/` — app source only (insight/viewer/libMovie/animationProducer), NOT the
    engine core source.
  - `bin/` — tools: `igen.exe`, `igbTypes.exe`, `sgOptimizer.exe`, `lua.exe`, `eventTracker.exe`.
  - `docs/` — PDFs (GettingStarted, UsersGuide).
  - `.igo` files beside headers are compiled meta-object descriptors (serialization schema).
- **raven-formats** (nikita488, MIT) at `tools/raven-formats/` — Python XMLB/engb/fb/zsnd
  read+write. Installed with `pip install -e tools/raven-formats`; CLI: `python3 -m raven_formats.xmlb -d in out`. Verified on PC `Data/colors.XMLB`, `herostat.engb/XMLB`.
- XMLB vs engb are the same format; engb uses global `@DATA@...` string-pool refs, XMLB
  inlines strings. Both decompile to identical XML modulo those refs.
- Community: XMLBCUI (old compiler), alchemymarvel.miraheze.org wiki, serptools IGB docs,
  igb-blender addon, `EthanReed517/XML2-Ultimate-Patch`.

## Conventions

- Repo root is CWD for all work. Machine-specific paths live only in `.env`
  (gitignored), template in `.env.example`.
- All run artifacts go to gitignored `scratch/` (structured by type), never `/tmp`.
- RE scripts in `tools/`; Ghidra project in `build/ghidra/`.
- `tools/pe.py` reads PE32 exports/imports/sections and generates proxy `.def`
  files; `tools/run_shim.sh` runs the game headless for A/B comparison.
- No copyrighted game assets committed to git; they stay in the game dirs referenced
  by `.env`.

## Current state

**The game runs, natively, and plays.** One headless run goes all the way
round: main menu → New Game → difficulty → the story cutscene → a level loaded,
rendered and simulated → the party dies with nobody driving them → the death
dialog → back to a fully rendered main menu. No Wine, no original D3D, and no
original machine code — the native build has no hybrid fallback, so every
instruction executed came from the translator. Drive a run and look at it
with the automatically published control channel (`tools/x2ctl.py probe`) -- a play-through
is an observation, never a gate.

`./run.sh` is the supported default launcher: with no arguments it builds when
needed and runs the current native SDL3 GPU game target. It records the exact
DirectInput states returned to the game under `scratch/recordings/`, and
`tools/x2ctl.py` discovers the live PID, port, and recording without manual
flags. Wine oracle/control workflows remain separate tools and cannot become an
accidental launcher mode.

What that does *not* mean. It is not playable in the sense that matters: a
person cannot yet pick it up and play, because the frame rate is ~30 fps
headless with the frame cap removed and nobody has driven a character with a
controller through a level. Controller enumeration, polling and hotswap run
end-to-end under a synthetic pad; an equivalent physical-pad play-through has
not been done. The
renderer accepts every draw the engine issues and reads every render state the
engine sets except fog and specular, which this title disables — but "nothing
is refused" is a statement about coverage, not about the picture being right.
See `docs/codemap.md` for the honest per-subsystem status and
`docs/info/claims/` for what has been proven, each with the observation that
would falsify it.

Earlier facts established during RE (see the durable claims under `docs/info/`):
- The game uses DirectInput 7 and 8. The native host serves both through one
  device implementation; joystick hotswap re-enters the game's own enumeration
  routine rather than writing its controller table from the host (C160/C161).
- `BUTTONS` enum + `ControllerType` meta-enum extracted from `libIGDisplay.dll`.
- Xbox `assetsfb.wad` is ZIP-like with a trailing block; entries are raw deflate;
  extractor in `tools/extract_wad.py`.
- The Xbox button art is not in the HUD font and is not addressable by existing
  medium-font codepoints. This port rasterises shared SVG equivalents into its
  own GPU atlas and inserts them at the RE'd Alchemy text-batch boundary; the
  shipped font assets remain untouched.
