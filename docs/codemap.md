# Codemap — what exists, where, and its honest status

Consult at the start of a task; update in the SAME commit that changes a
subsystem. Companion registries: `docs/re-frontier.md` (ordered RE progress,
real vs hack) and `docs/info/` (claims + instruments).

Direction is **static recompilation + native overrides**. The PC build was the
original target; since C010 was falsified the **Xbox build is the live front**
(an existing recompiler, `vendor/xboxrecomp`, removes the cost that argument
rested on) — see [`strategy.md`](strategy.md). Status words mean:
**verified** = checked against the original on real data, with the check cited ·
**partial** = works, with a named gap · **untouched** = not started.

## Recompiler (`tools/`)

| what | where | status |
|---|---|---|
| PE32 reader: sections, imports, exports, IAT, proxy `.def` | `tools/pe.py` | **verified** — export count agrees with winedump; ordinal-import detector run against both classes (I003) |
| Function/instruction export from Ghidra | `tools/ghidra_scripts/ExportFuncs.py` | **partial** — boundaries+instructions+call targets; 77–80% of exec bytes covered |
| x86-32 → C translator | `tools/recomp.py` | **partial** — 500/521 libIGDisplay functions (96%); x87, SBB, REP string ops unhandled (I005) |
| Interop: export shims + import stubs | `tools/recomp.py dll` | **verified** — ESP-switch both ways, callee-driven cleanup; no arg counts needed (C014) |
| Runtime: CPU state, lazy flags, dispatch | `src/recomp/x86rt.h` | **partial** — flags verified indirectly via difftest; x87 state absent |
| Instruction histogram (sound, not linear sweep) | `tools/ghidra_scripts/InstrHisto.py` | **verified** — C011 |

## Verification

| what | where | status |
|---|---|---|
| Differential test vs the original DLL | `tests/difftest.c` | **verified** — 116 functions, forced relocation, memory-write comparison; negative controls fire (I006, C016) |
| Wine oracle, headless, muted, multi-sample | `tools/run_shim.sh` | **verified** — I007 (supersedes distrusted I002) |
| DLL drop-in staging | `tools/build_shim.sh` | **partial** — proxy and trace modes; recomp staged by hand |
| Boundary call tracer | `tools/gen_trace.py` | **verified** — I004; "never called" summary still unreachable (harness SIGKILLs) |

## Recompiled output (`src/recomp/`, gitignored — regenerate, never edit)

| module | status |
|---|---|
| `libIGDisplay.dll` | **partial** — 116 verified functions live in the game, 704 exports forwarded to the original. All-500 build page-faults, not yet bisected |
| `XMen2.exe` | **untouched** — 11,106 functions, 643,647 instructions, the eventual target |
| other 15 `libIG*.dll` | **untouched** |

## Xbox recompilation (`xbox/`, `vendor/xboxrecomp` — gitignored, see `patches/`)

| what | where | status |
|---|---|---|
| Xbox game project (entry, VEH, ICALL diagnostics) | `xbox/src/main.c`, `recomp_manual.c`, `recomp_types.h` | **partial** — builds a 19 MB native PIE Linux executable that runs game code (C038, C039) |
| Lift pipeline: disasm → func_id → recomp | `tools/xbox_relift.sh` | **verified** — 24,663/24,663 functions across all 11 executable sections, 0 failures; fails loudly if a seed does not land (C049) |
| Runtime discovery of statically-invisible functions | `xbox/seeds.json`, `tools/xbox_discover.sh` | **verified** — 23 functions observed at runtime and fed back (C040, C041) |
| Bulk vtable harvest | `tools/xbox_vtable_seeds.py` | **verified** — 1288 missing functions in one pass, every filter's rejection count printed (C054) |
| Unresolved-indirect-call tally | `xbox/src/recomp_manual.c` | **verified** — I008; fatal by default (`XBOX_ICALL_CONTINUE=1` to survey); `XBOX_ICALL_SELFTEST=1` proves both miss paths fire |
| Runtime discovery loop, automated | `tools/xbox_discover.sh` | **verified** — run → seed → re-lift → repeat; stops on convergence, on a repeat, or on an out-of-image target, and says which |
| Register model | `xbox/src/recomp_types.h` | **verified** — every register global including ebp; the g_seh_ebp bridge is gone (C051) |
| Branch conditions | `patches/xboxrecomp/0003-*.patch` | **verified** — deferred `cmp` operands snapshotted at the flag setter; 216 wrong-direction branches fixed (C050) |
| Kernel bridge ordinal tables | `patches/xboxrecomp/0002-*.patch` | **partial** — names validated against the 371-entry export table (I009, C043); bridge *semantics* unaudited |
| Function boundary detection | `patches/xboxrecomp/0003-*.patch` | **verified** — flow-following end detection; silently-empty stubs 7998 → 348 (C048) |
| Vectored exception handling on Linux | `patches/xboxrecomp/0004-*.patch` | **verified** — `sigaction`-based; was a stub returning NULL, so every handler was discarded. The crash reporter now fires (C055) |
| NV2A GPU emulation | `vendor/xboxrecomp/src/nv2a`, wired in `xbox/src/main.c` | **partial** — initialises (VRAM 64 MB, RAMIN 1 MB, MMIO hook) and the VEH routes GPU faults to it; the decoder is no longer `#if _WIN32`. Not yet exercised: the title dies in the CRT heap first (C053, C055) |
| Host D3D8 → OpenGL | `vendor/xboxrecomp/src/d3d` | reference — the NV2A PGRAPH translator emits onto this device; not called directly by game code |
| Xbox game execution | — | **partial** — 25,777 functions, 476 kernel calls, 4077 indirect calls with **zero unresolved**; faults in the CRT heap unlinking a block whose header is a .text address, so its forward pointer is int3 padding. Nothing renders |

Vendored toolkit changes live as patches in `patches/xboxrecomp/` because
`vendor/` is gitignored; re-apply them after re-cloning the toolkit.

## Assets and engine RE (pre-dates the recomp direction, still valid)

| what | where | status |
|---|---|---|
| IGB container, DXT, meshes | `src/core/igb*.c` | **partial** — renders real level geometry in `flyview`/`meshview` |
| Enbaya animation decode | `src/core/igb_anim.c` | **partial** — full VLC stream decoder, `tests/test_enbaya.c` |
| XMLB/engb read | `tools/raven-formats/` (vendored, MIT) | **verified** on real assets |
| Xbox WAD extraction | `tools/extract_wad.py` | **partial** |
| ARK meta-object system | `docs/RE/ark.md` | **verified** — registration, construction, `+0x3c` impl redirect (C008, C009) |
| Alchemy 5.0 headers | `scratch/ref/alchemy5/` (gitignored) | reference |

## Not started

Host reimplementation of any Win32/D3D8/DirectInput call (imports currently
call the real ones); audio; the three shipped features (controller hotswap,
auto-mapping, Xbox button prompts) which land as overrides once enough of the
input path is recompiled.
