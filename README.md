# X-Men Legends II — Alchemy SDL Remaster

Reimplement the Alchemy engine that runs X-Men Legends II: Rise of Apocalypse (2005,
Activision / Raven / Vicarious Visions) as **native SDL2 code**, driven by the PC
build's game data and the Xbox build's authentic assets. The game runs without any of
the original Windows binaries. This is the decided direction (option B: SDL engine
reimplementation) — not a byte-for-byte static recompile of the x86 binaries (option A,
rejected as a multi-year research project with no precedent on x86).

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

- **Oracle**: the original PC build under Wine (Lutris config exists). A differential
  harness runs oracle + our build through the same deterministic scenario and compares.
  Oracle baseline is M2; the harness is built before any engine code (recomp-harness
  discipline: verify before you declare done).

## Roadmap

- [x] **M0** Repo + plan (this file)
- [ ] **M1** Acquire Alchemy 5.0 Kit reference
- [ ] **M2** Oracle baseline — PC build headless under Wine, deterministic capture
- [ ] **M3** Data layer — XMLB/engb/file-package readers (loose files + assetsfb.wad)
- [ ] **M4** Input layer — SDL_GameController → engine callbacks; the 3 features land here
- [ ] **M5** Render layer — IGB textures/meshes → SDL renderer
- [ ] **M6** Audio layer
- [ ] **M7** Main loop + game boots to title

## Conventions

- Repo root is CWD for all work. Machine-specific paths live only in `.env`
  (gitignored), template in `.env.example`.
- All run artifacts go to gitignored `scratch/` (structured by type), never `/tmp`.
- RE scripts in `tools/`; Ghidra project in `build/ghidra/`.
- No copyrighted game assets committed to git; they stay in the game dirs referenced
  by `.env`.

## Current state

Facts established so far (see `scratch/logs/`):
- Input layer is DirectInput 7 only (`DINPUT.dll::DirectInputCreateEx`); joystick
  enumeration callback `createControllers @ 0x100052a0`; no hotplug support shipped.
- `BUTTONS` enum + `ControllerType` meta-enum extracted from `libIGDisplay.dll`.
- Xbox `assetsfb.wad` is ZIP-like with a trailing block; entries are raw deflate;
  extractor in `tools/extract_wad.py`.
- `x2f_hud_xbox.igb` (93,288 B) differs from PC `x2f_hud.IGB` (93,252 B) — Xbox glyph
  texture. Font XMLB is byte-identical across builds (glyphs live only in the IGB).
