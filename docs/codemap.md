# Codemap — what exists, where, and its honest status

Consult at the start of a task; update in the SAME commit that changes a
subsystem. Companion registries: `docs/re-frontier.md` (ordered RE progress,
real vs hack) and `docs/info/` (claims + instruments).

Direction is **static recompilation of the PC build + native overrides** —
see [`strategy.md`](strategy.md). Status words mean:
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
