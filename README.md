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

**No game content is distributed here.** This repository contains only original
code: the recompiler, the native host, the asset readers and the RE notes. It
ships no executables, no libraries, no textures, no audio and no data files from
the game, and none has ever been committed — every build reads them from a copy
you already own, located through a gitignored `.env` (see `.env.example`). The
install directory is treated as strictly read-only; run directories are symlink
farms over it and nothing is ever written back.

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

Where this is going next -- performance, load time, a real input-binding UI,
launching straight into a game -- is [`docs/roadmap.md`](docs/roadmap.md), with
honest status against each.

## The three features (all land in the input layer)

1. **Controller hotswap** — implemented through SDL3 and the game's own
   DirectInput enumeration/connection callbacks; late attach and detach are
   exercised by a frame-scheduled virtual pad.
2. **Controller defaults UI** — implemented at the retained PC controller
   editor: Keyboard Defaults keeps the shipped keyboard table and Xbox Defaults
   applies the assignments recovered from the Xbox executable—not a modern
   mapping invented by the port. Black/White pack use remains a direct-action
   RE boundary because the PC binding table has no Health/Energy rows.
3. **Xbox button prompts** — this port's SVGs are published into unused bytes
   of the PC font and returned at the game's RE'd physical-input naming
   boundary only for SDL-classified Xbox controllers. Delivery and the 21-row
   bindable Xbox preset are implemented; an in-game prompt capture remains the
   visible-use gate.

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
flags. `./run.sh wine` and
`./run.sh stock` are explicitly named oracle/control paths; neither can become
the accidental default.

What that does *not* mean. It is not playable in the sense that matters: a
person cannot yet pick it up and play, because the frame rate is ~30 fps
headless with the frame cap removed and nobody has driven a character with a
controller through a level. Controller enumeration, polling and hotswap run
end-to-end under a synthetic pad; the remaining controller work is the default
binding/prompt experience. The
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
  medium-font codepoints. This port owns SVG equivalents under `assets/buttons/`
  and publishes them into verified-unused font cells with `make_pad_font.py`.
