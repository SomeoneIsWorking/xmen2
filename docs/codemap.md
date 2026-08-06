# Codemap — what exists, where, and its honest status

Consult at the start of a task; update in the SAME commit that changes a
subsystem. Companion registries: `docs/re-frontier.md` (ordered RE progress,
real vs hack) and `docs/info/` (claims + instruments).

Direction is **static recompilation + native overrides**, and as of 2026-08-05
the **PC build is the live front again** (C078). The reason is not preference:
on the PC path the game *already runs*, so functions move from "forwarded to
the original DLL" to "our recompiled C" while it keeps working; on the Xbox
path nothing works until everything does, and its GPU boundary is NV2A push
buffers, which [`strategy.md`](strategy.md) calls "the xemu problem". The Xbox
work is real and kept (it boots deep into engine setup with a clean register
file) but it is not the shortest road to a playable port. Status words mean:
**verified** = checked against the original on real data, with the check cited ·
**partial** = works, with a named gap · **untouched** = not started.

## Recompiler (`tools/`)

| what | where | status |
|---|---|---|
| PE32 reader: sections, imports, exports, IAT, proxy `.def` | `tools/pe.py` | **verified** — export count agrees with winedump; ordinal-import detector run against both classes (I003) |
| Function/instruction export from Ghidra | `tools/ghidra_scripts/ExportFuncs.py` | **partial** — boundaries+instructions+call targets; 77–80% of exec bytes covered |
| x86-32 → C translator | `tools/recomp.py` | **partial** — C084; 8 modules, 36,048/36,340 functions (99.3–100% each), 97.8–99.9% of instructions. What remains is SSE/MMX and 3DNow! (an AMD path selected on CPUID), plus FPTAN, PUSHAD, RCR. `emit --split N` chunks the output: the exe alone is 2.05M lines and cost 94 s / 4.8 GB as one TU |
| Truncated-body repair | `tools/ghidra_scripts/MergeTruncated.py`, `tools/recomp.py` | **verified** — C086; absorbs a spurious function detected inside another (42 → 7 in XMen2), refusing when the inner one has real callers. Bodies that still end without a terminator are emitted as an explicit fall-through, which is what the hardware does |
| Bulk vtable seeding | `tools/ghidra_scripts/SeedPointerTables.py` | **verified** — C085; runs of >=3 consecutive code pointers in read-only data. 3,024 new functions in XMen2.exe from one pass where the runtime finds one per rebuild. 99.9% of the resulting functions start with a plausible prologue; 2 spurious of 14,840 |
| Native discovery loop | `tools/native_discover.sh` | **verified** — feeds the runtime's missing-constructor report back into Ghidra as seeds and rebuilds, until a round finds nothing. Reports module + GUEST address, since modules are relocated and a seed must name the linked address |
| Interop: export shims + import stubs | `tools/recomp.py dll` | **verified** — ESP-switch both ways, callee-driven cleanup; no arg counts needed (C014). The shim now `jmp`s to `x86_enter_tramp` and pushes nothing that has to outlive the body: everything below the entry ESP belongs to the guest and to any host callee (C080) |
| Runtime: CPU state, lazy flags, dispatch | `src/recomp/x86rt.h` | **partial** — flags verified indirectly via difftest; x87 state absent |
| Private per-thread runtime stack | `tools/recomp.py` (`x86_rt_stack_*`, `x86_enter_tramp`) | **verified** — C080; the CPU struct and every runtime frame moved off the guest stack, which is what made the full 521-function build run. Refuses to run a body without one rather than falling back |
| Instruction histogram (sound, not linear sweep) | `tools/ghidra_scripts/InstrHisto.py` | **verified** — C011 |

## Verification

| what | where | status |
|---|---|---|
| Differential test vs the original DLL | `tests/difftest.c` | **verified** — 116 functions, forced relocation, memory-write comparison; negative controls fire (I006, C016) |
| Hybrid recomp DLL build, one command | `tools/build_recomp.sh` | **verified** — emit + runtime + dll + compile + stage, parameterised by the entry-point set; reproduces the running 156-function build (C078) |
| Grow the recompiled set / find what breaks it | `tools/bisect_recomp.sh` | **verified but no longer needed for this** — delta-debugging with the real game as the verdict (loaded / alive / not-uniform), both controls measured first. Its premise was wrong: the 'independent culprits' were all just functions that call a host function, and the defect was the entry path (C080), not the set |
| Entry/exit watch on recompiled entry points | `src/x86watch.c` | **verified** — I019; `X2_WATCH=0x…`, both directions self-tested in the shipping DLL. Writes to a FILE: the game is a GUI-subsystem process with no stderr, and the stderr version was silently empty. Also reports where the runtime's C frame sits relative to `guest_esp` (`[STACK]`), which is what caught C080 |
| Which recompiled bodies were ever entered, how often, and in what order | `src/native/x86rt_native.c` (`-DX2_NATIVE_REACHED=ON`, `X2_REACHED`) | **verified** — I021; a SET with first-entry ordering and call counts, not a history, because the ring evicts and so cannot answer "was this ever called". Keyed on (entry point, MODULE BASE): every `libIG*.dll` links for 0x10000000, so an ep alone is not unique and one module's count was reported spanning two. Reports the distinct-pair count as its denominator, one line per module. `X2_REACHED_SELFTEST=1` checks reached, never-reached, ordering, counts AND that the same ep in two modules stays apart, and exits 4 if any fails. Answered the question issue #14 turned on and then localised its cause |
| Guest-memory peek at the failure | `src/native/x86rt_native.c` (`X2_PEEK=libIGCore+0x15f3fc:1`) | **verified** — I022; `<module>+0x…` resolves through the module's actual mapped base, so a Ghidra address can be pasted in. Reads via `process_vm_readv`, so it is safe from the SIGSEGV handler and an unmapped address says so instead of faulting again |
| Guest registers at a fault | `src/native/x86rt_native.c` (`x86_regs_dump`) | **verified** — I024; the register file of the last body to cross the host boundary, which guest-to-guest calls share because they pass the same `CPU*` down. States that limit in its own output. Identified issue #15's faulting operand (`edi=0` matching a fault at nil) after reasoning about the disassembly had produced a contradiction |
| Exact fault line in a generated chunk | `CMakeLists.txt` (`-DX2_NATIVE_O0=<chunk>.c`) | **verified** — I025; compiles ONE 200k-line generated chunk at -O0 so `addr2line` is exact, since whole-build -O0 over 2M lines is not an option. FATAL_ERRORs on a chunk name that does not exist, so a typo cannot yield an ordinary build that is then read as exact |
| Per-body boundary ring (native) | `src/native/x86rt_native.c` (`-DX2_NATIVE_TRACE=ON`) | **verified after a fix** — I026; records enter/exit of every recompiled body with esp. It was misattributing every per-body entry, decoding a LINKED ep as a mapped address (libIGCore functions labelled libIGUtils); it now records the module base alongside the ep. Mutually exclusive with the reached set — both claim `X86_ENTER_FN` and CMake refuses the combination |
| Argument watch (native) | `src/native/x86rt_native.c` (`X2_ARGS=0x…`, trace builds) | **verified** — I027; the native counterpart of the hosted `X2_WATCH`. ECX plus four stack words on entry, EAX on exit. Says explicitly when no watched entry point was entered, and states in its banner that it cannot know the real argument count. Produced issue #15's whole allocator history |
| Native fault reporter | `src/native/x2native.c` (`poison_sigsegv`) | **verified** — a non-import SIGSEGV now prints the host rip, a *runnable* `addr2line` command (the load base subtracted, since the binary is PIE), the boundary ring and the peek. It named `trimAll` from the bare "SIGSEGV at 0x4" that was previously the whole report. States its own blind spot when built without the trace |
| In-process crash reporter | `src/x86fault.c` | **verified** — I020; names the module a fault EIP falls in, annotates stack slots with the call site that pushed them, and dumps the last 64 recompiled/host boundary crossings. Exists because winedbg under `wine explorer /desktop=` produces no output at all |
| Wine oracle, headless, muted, multi-sample | `tools/run_shim.sh` | **verified** — I007 (supersedes distrusted I002) |
| Export provenance check | `tools/verify_export.py` | **verified** — compares each JSON's block layout against the shipped PE's sections; 10 of 10 agree. Wired into the discovery loop so it cannot seed against the wrong image (issue #12) |
| KERNEL32 on POSIX | `src/native/kernel32.c` | **partial** — the exe's 39 entry points plus the DLLs' heap and virtual-memory surface (GetProcessHeap/HeapAlloc/HeapFree, VirtualAlloc/Free/Query, GlobalMemoryStatus). VirtualQuery answers about the GUEST address space and reports whole free spans, because a per-page answer turns a caller's address-space scan into a million iterations. It answers from the SAME reservation table VirtualAlloc maintains (C089) — it did not, so memory the guest had just reserved read back as MEM_FREE and libIGCore's CRT heap-grow scan never terminated (67 VirtualAlloc calls and ~527 MB, now 2). Also: time, files, critical sections, version/feature queries. Case-insensitive path resolution, because the game's paths do not match the extracted files and a failed open surfaces as a missing texture, not as "file not found". LoadLibrary/GetProcAddress, file mapping and directory enumeration stop by name |
| MSVCR71 on libc | `src/native/crt.c` | **partial** — 79 of the exe's 87 CRT imports. C++ EH and the varargs family stop by name rather than faking |
| Win32 surface on SDL3 | `src/native/win32_sdl.c` | **partial** — 25 of the 43 non-game imports implemented (CRT, KERNEL32, USER32 window/geometry/cursor). The other 138 stubs stay WEAK and abort by name, so a real implementation overrides one by existing. Verified: 25 strong vs 138 weak in the linked binary, plus known-answer stdcall/cdecl cleanup checks |
| Module export to JSON, as a command | `tools/ghidra_export.sh` | **verified** — libIGCore: 5818 functions, 151,635 instructions. Refuses a zero-function export rather than handing the recompiler an "empty" module |
| Guest heap | `src/native/guest_heap.c` | **verified** — C083; first-fit with coalescing over a 256 MB arena at 0x40000000, because host malloc returns addresses above 4 GB. Magic-word headers catch double-free and foreign pointers. 6 battery checks |
| PE relocation on a moved module | `src/native/pe_map.c` | **verified** — C082; 2460 HIGHLOW fixups applied when libIGDisplay moved. Refuses to place a module that must move and has no .reloc directory |
| Native module initialisation | `src/native/x2native.c` (`modules_init`) | **verified** — both modules run DllMainCRTStartup in dependency order read from their import tables; libIGCore's 51 static constructors execute and DllMain returns TRUE. FS:[0] is backed by a real TIB word for the SEH prologues; exception DELIVERY is not provided |
| Cross-module import binding (native) | `src/native/pe_map.c`, `x86rt_native.c` | **verified** — the host binds every IAT slot as a loader would: 104 bound to recompiled bodies. Unresolvable slots point into a PROT_NONE page, one address per import, so using one faults with the module and symbol named instead of reading as NULL. Needed because 40 of libIGDisplay's slots are read as DATA in 113 places |
| Multi-module native runtime | `src/native/x86rt_native.{c,h}` | **verified** — one dispatcher over several modules, keyed on MAPPED address because every libIG*.dll is linked for 0x10000000 and their entry points collide. libIGDisplay (521 bodies) + libIGCore (5769 of 5818, 99.2%) link and run in one 14 MB ELF; the relocated module's absolute references resolve against its own base |
| Native (Wine-free) host: PE mapper + battery | `src/native/`, `tools/recomp.py native` | **partial** — C081; `x2native` is a real x86-64 ELF that maps libIGDisplay.dll at 0x10000000 and runs recompiled bodies against it. 14 postcondition checks pass and `--selftest` proves they bind. 107 imports abort by name; no game runs yet. Identity mapping needs the low 4 GB, which macOS reserves (issue #10) |
| Controller backend | `src/display/ig_sdl_controller.c` | **verified** — SDL3 gamepad API, exercised by `test_controller` through a virtual joystick. The port path (this + `x2native`) is SDL3; the asset viewers are separate binaries still on SDL2 |
| Play a build on a real screen, with sound | `run.sh` | **verified** — `./run.sh` plays the ALL build, `./run.sh stock` the untouched install as a control. The looking counterpart to `tools/run_shim.sh`, which is headless and for measuring |
| DLL drop-in staging | `tools/build_shim.sh` | **partial** — proxy and trace modes; recomp staged by hand |
| Boundary call tracer | `tools/gen_trace.py` | **verified** — I004; "never called" summary still unreachable (harness SIGKILLs) |

## Recompiled output (`src/recomp/`, gitignored — regenerate, never edit)

| module | status |
|---|---|
| `libIGDisplay.dll` | **partial** — ALL 521 translatable functions recompiled and the game renders (C080, issue #9 resolved). 514 export shims cover 598 of the 748 exported names; 150 are still forwarded. Survival is no longer the question; coverage is: only 9 distinct recompiled entry points are actually entered in a 30-second intro run, so most bodies are unexercised rather than verified |
| `XMen2.exe` | **untouched** — 11,106 functions, 643,647 instructions, the eventual target |
| other 15 `libIG*.dll` | **untouched** |

## Xbox recompilation (`xbox/`, `vendor/xboxrecomp` — gitignored, see `patches/`)

| what | where | status |
|---|---|---|
| Xbox game project (entry, VEH, ICALL diagnostics) | `xbox/src/main.c`, `recomp_manual.c`, `recomp_types.h` | **partial** — builds a 19 MB native PIE Linux executable that runs game code (C038, C039) |
| Lift pipeline: disasm → func_id → recomp | `tools/xbox_relift.sh` | **verified** — 24,663/24,663 functions across all 11 executable sections, 0 failures; fails loudly if a seed does not land (C049) |
| Runtime discovery of statically-invisible functions | `xbox/seeds.json`, `tools/xbox_discover.sh` | **verified** — 23 functions observed at runtime and fed back (C040, C041) |
| Bulk vtable harvest | `tools/xbox_vtable_seeds.py` | **verified** — 1288 missing functions in one pass, every filter's rejection count printed (C054) |
| Function detection behind embedded data | `patches/xboxrecomp/0003-*.patch` | **verified** — a candidate the linear sweep desynchronised past is decoded from its own address; 148 recovered, and the 34 that still decode to nothing are printed by address rather than dropped (C073) |
| Unresolved-indirect-call tally | `xbox/src/recomp_manual.c` | **verified** — I008; fatal by default (`XBOX_ICALL_CONTINUE=1` to survey); `XBOX_ICALL_SELFTEST=1` proves both miss paths fire |
| Guest call-site attribution on every call | `xbox/src/recomp_manual.c`, lifter | **verified** — I018; every `RECOMP_DCALL`/`ICALL_SAFE`/`ITAIL` carries the call instruction's own VA, so a failure reads `guest 0x002A975F (sub_002A9570+0x1EF)`. The native stack cannot supply this: a compiled-C offset does not map to a guest offset |
| Indirect-call argument/return watch | `xbox/src/recomp_manual.c` | **verified** — I012; `XBOX_ICALL_WATCH=0x…,0x…` prints args and eax per call, `XBOX_ICALL_WATCH_SELFTEST=1` proves both the positive and the NEVER-CALLED negative. Use it instead of gdb line breakpoints, which lie on this -O2 build |
| Runtime discovery loop, automated | `tools/xbox_discover.sh` | **verified** — run → seed → re-lift → repeat; stops on convergence, on a repeat, or on an out-of-image target, and says which |
| Register model | `xbox/src/recomp_types.h` | **verified** — every register global including ebp; the g_seh_ebp bridge is gone (C051) |
| Branch conditions | `patches/xboxrecomp/0003-*.patch` | **verified** — operands snapshotted at the flag setter (C050); flag state propagated over the CFG, not just fall-through, killing always-false branches (C059) |
| Vendor patch reproduction check | `tools/check_patches.sh` | **verified** — applies the patches to the pinned upstream commit and diffs against `vendor/`; caught patch 0003 missing six files. Fails loudly when the clone is pristine (nothing verified) |
| Recompiler test suite | `vendor/xboxrecomp/tools/recomp/test_*.py` | **verified** — I010; 15 tests in ~1s, real-binary regressions skip loudly when the XBE is absent; `tools/xbox_relift.sh` refuses to lift if they fail |
| Callee-saved + stack-bounds checks | `xbox/src/recomp_manual.c` | **verified** — I011; every indirect call checked, clean case stated; found the ordinal-217 defect (C060) |
| Kernel bridge ordinal tables | `patches/xboxrecomp/0002-*.patch` | **partial** — names validated against the 371-entry export table (I009, C043); bridge *semantics* unaudited |
| Placed virtual reservations | `patches/xboxrecomp/0005-*.patch` | **verified** — the query and the allocator describe the SAME address space: only the arena's unused tail is MEM_FREE, and a reservation that names its address gets that address or nothing (C070, C071) |
| Function boundary detection | `patches/xboxrecomp/0003-*.patch` | **verified** — flow-following end detection, and a body may now cross a detected function that sits INSIDE it (an interior branch lands mid-block; a tail call lands on a function start). A crossing is bounded to ONE intervening function and one per body (30 crossings, 623 refusals, both printed) — unbounded it grew a 5-byte thunk to 872 KB. Silently-empty stubs 7998 → 348 → 277, and none is called (C048, C077, issue #8) |
| Vectored exception handling on Linux | `patches/xboxrecomp/0004-*.patch` | **verified** — `sigaction`-based; was a stub returning NULL, so every handler was discarded. The crash reporter now fires (C055) |
| NV2A GPU emulation | `vendor/xboxrecomp/src/nv2a`, wired in `xbox/src/main.c` | **partial** — initialises (VRAM 64 MB, RAMIN 1 MB, MMIO hook) and the VEH routes GPU faults to it; the decoder is no longer `#if _WIN32`. Not yet exercised: the title dies in the CRT heap first (C053, C055) |
| Host D3D8 → OpenGL | `vendor/xboxrecomp/src/d3d` | reference — the NV2A PGRAPH translator emits onto this device; not called directly by game code |
| Native CRT heap override | `xbox/src/recomp_manual.c` (`-Wl,--wrap`) | **debt** — bypasses the C056 heap defect; recompiled bodies still linked, `XBOX_NATIVE_HEAP=0` restores them (C057, `xb-nheap`) |
| Empty-stub reporter | generated `recomp_stubs_unresolved.c` | **verified** — the 276 undetected-call stubs report themselves; found 0x0010C470 immediately (C058) |
| Xbox game execution | — | **partial** — 67262 indirect calls, **every one of 94088 checked calls restores ebx/esi/edi/ebp**, esp stayed in the guest stack throughout, no empty stub called. Stops on one missing function (0x0029CA50), which is ordinary discovery-loop input. Nothing renders yet |

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
