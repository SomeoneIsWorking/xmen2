# X-Men Legends II — native PC port

Turn X-Men Legends II: Rise of Apocalypse (2005, Activision / Raven / Vicarious
Visions) into a **native, buildable codebase** by statically recompiling the PC
build's x86 machine code to C and then replacing subsystems with hand-written
native code. The end state runs without Wine and without the original binaries;
the Xbox build supplies authentic assets (button glyphs).

**Direction: static recompilation of the PC build to native C, then native
overrides — see [`docs/strategy.md`](docs/strategy.md).** The whole binary is
translated mechanically so the game runs early, then subsystems are replaced
with hand-written C while the rest keeps working. Measured feasible: Ghidra
identifies function bodies covering 77.5% of `XMen2.exe` (11,106 functions,
643,647 instructions) despite the exe exporting no symbols at all, and a decoder
covering ~80 x86 mnemonics reaches 99.7% of them.

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
- **Xbox ISO** (`$XBOX_ISO`): `default.xbe` (5.7 MB), `z/assetsfb.wad` (690 MB asset
  package), `fonts_XBOX` set incl. `x2f_hud_xbox.igb` — contains the **authentic Xbox
  button glyphs** the remaster needs for prompts.
- **Alchemy 5.0 Kit** (archive.org `alchemy-kit_202309`, 152 MB): engine source + docs.
  The architectural Rosetta stone. XML2 ships Alchemy 3.2; 5.0 assets are version-
  incompatible but the engine architecture (igCore/igDisplay class model, IGB format,
  file-package system, DLL boundary) is directly analogous.

## The three features (all land in the input layer)

1. **Controller hotswap** — SDL_GameController device add/remove events. The engine
   already has the hooks: `libIGDisplay` exposes `_controllerConnectionFunction` /
   `_controllerDisconnectionFunction` on `igControllerManager`; the shipped code just
   never wires them (no `WM_DEVICECHANGE` anywhere — controllers are enumerated once).
2. **Auto controller mapping** — SDL gamecontrollerdb + `SDL_GAMECONTROLLERCONFIG`.
3. **Xbox button prompts** — glyphs from `x2f_hud_xbox.igb` replacing the PC
   `Texs/joy1..4.png` (which are just digits 1–4).

## Verification

- **Oracle**: the original PC build under Wine. `tools/run_shim.sh <rundir>` runs it
  headless on Xvfb and captures a frame; `scratch/run/stock` is the unmodified
  reference and `scratch/run/proxy` swaps in our DLL. Both are symlink farms — the
  real game install is never modified.
- **Ledgers**: `docs/info/` holds what has been *proven* (claims, each with the
  observation that would falsify it) and which tools can be *trusted* (instruments).
  Query with `info.py brief <words>` before re-deriving anything.

## Roadmap

- [x] **M0** Repo + plan (this file)
- [x] **M1** Acquire Alchemy 5.0 Kit reference
- [x] **M3a** Data layer READ: XMLB/engb decompile via raven-formats (MIT); fb/zsnd also covered
- [ ] **M3b** Data layer WRITE: compile path verified (need round-trip test on a real asset)
- [x] **M2** Oracle baseline — PC build runs headless under Wine (DXVK + lavapipe
      + virtual desktop), frames captured. `tools/run_shim.sh`. NOT yet
      deterministic: boot-movie timing varies between runs.
- [x] **M2b** DLL-swap mechanism — pass-through proxy `libIGDisplay.dll` (898
      forwarded exports) verified transparent in the real game
- [x] **M3c** ARK meta-object system reverse-engineered (`docs/RE/ark.md`)
- [ ] **M4** Recompiler: x86-32 decoder + C emitter (`tools/re_frontier.py next`)
- [ ] **M5** Host layer for the 989 imported Win32/D3D8/DInput/CRT symbols
- [ ] **M6** Recompiled `libIGDisplay.dll` runs in the real game (proving ground)
- [ ] **M7** Recompiled `XMen2.exe`
- [ ] **M8** Native overrides — the 3 controller features land here

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
instruction executed came from the translator. `tools/smoke_loop.sh` drives
that run and checks it; `tools/smoke_loop.sh --selftest` proves its checks can
fail and needs neither the game nor a GPU.

What that does *not* mean. It is not playable in the sense that matters: a
person cannot yet pick it up and play, because the frame rate is ~30 fps
headless with the frame cap removed, the three shipped features are not built,
and nobody has driven a character with a controller through a level. The
renderer accepts every draw the engine issues and reads every render state the
engine sets except fog and specular, which this title disables — but "nothing
is refused" is a statement about coverage, not about the picture being right.
See `docs/codemap.md` for the honest per-subsystem status and
`docs/info/claims/` for what has been proven, each with the observation that
would falsify it.

Earlier facts established during RE (see `scratch/logs/`):
- Input layer is DirectInput 7 only (`DINPUT.dll::DirectInputCreateEx`); joystick
  enumeration callback `createControllers @ 0x100052a0`; no hotplug support shipped.
- `BUTTONS` enum + `ControllerType` meta-enum extracted from `libIGDisplay.dll`.
- Xbox `assetsfb.wad` is ZIP-like with a trailing block; entries are raw deflate;
  extractor in `tools/extract_wad.py`.
- `x2f_hud_xbox.igb` (93,288 B) differs from PC `x2f_hud.IGB` (93,252 B) — Xbox glyph
  texture. Font XMLB is byte-identical across builds (glyphs live only in the IGB).
