# RE Frontier — the ordered RE dependency chain toward a faithful BL2

Tracked by `tools/re_frontier.py` (consult it FIRST; update it in the SAME commit
that changes a step). This is the fine-grained companion to `docs/codemap.md`:
the codemap says *what subsystem exists*, this says *which ordered RE step is
real reverse-engineering vs a hack that jumped ahead*.

**Hard rule (no hacks / no fallbacks):** a `⛔ hack` status is DEBT, never an
acceptable resting state. It marks a shortcut standing in for absent RE and MUST
be removed as its real mechanism lands. `re_frontier.py hacks` is the debt list;
`re_frontier.py next` tells you the next RE-ready step.

**`re-verified` MEANS FAITHFUL to the real target — not "the mechanism runs."** A
step is `re-verified` only when its OUTPUT matches the real game/binary (look /
sound / behavior) on real data. An internal trace ("bytecode reached the call
site", "N rows attached") is a mechanism check, NOT faithfulness — if it runs but
the result doesn't match the real target, it is `re-partial` with the
faithfulness gap named. The user observes the running system; that observation
overrides any internal trace.

**Fail fast & loud:** a failure must surface loudly, never silently fall back —
unless the fallback IS intended behavior of the real target being reproduced.

Statuses: ✅ re-verified · 🟡 re-partial (honest gap) · 🔬 in-progress ·
⛔ hack (debt, must remove) · ⬜ todo · ➖ skip-by-design · ⏸ blocked (computed).

<!-- Machine-edited by tools/re_frontier.py add/set. Format: `## <area>` sections;
     each entry is `### <id> — <title>` followed by `- <field>: <value>` lines. -->

## assets

### igb-read — IGB container + DXT + mesh + Enbaya animation decode
- status: re-verified
- deps: 
- evidence: src/core/igb*.c; meshview/flyview render real level geometry and textures from shipped .igb; tests/test_enbaya.c
- where: src/core/
- gap: 
- notes: 


## engine

### ark — Alchemy ARK meta-object system: how a class registers with libIGCore
- status: re-verified
- deps: abi
- evidence: C008/C009; docs/RE/ark.md; decompiled from libIGDisplay.dll and libIGCore.dll
- where: docs/RE/ark.md
- gap: Mechanism is READ, not yet EXERCISED -- no class has been registered by our own code. igObject::constructDerived is still unread.
- notes: 

### vtable — MSVC vtable layout for a replaced igDisplay class
- status: skip-by-design
- deps: ark
- evidence: 
- where: tools/ghidra_scripts/DumpVtab.py
- gap: Superseded by the recomp direction: recompiled code reproduces the original vtables byte-for-byte, so their layout no longer has to be reverse-engineered. Re-open only if a hand-written OVERRIDE needs to construct an Alchemy object itself.
- notes: 

### constructderived — igObject::constructDerived -- how libIGCore finishes an object
- status: skip-by-design
- deps: ark
- evidence: 
- where: 
- gap: Superseded by recomp; only needed if an override constructs objects itself.
- notes: 


## input

### ctrlmgr — Native igControllerManager / igWin32ControllerManager
- status: skip-by-design
- deps: vtable
- evidence: 
- where: 
- gap: Superseded: under recomp+overrides the controller manager is replaced as an override on recompiled functions, not as a hand-built class registered with libIGCore.
- notes: 

### sdl-input — SDL_GameController backend + the three shipped features
- status: todo
- deps: rc-overrides
- evidence: 
- where: 
- gap: Now lands as a native override over recompiled input functions.
- notes: 


## harness

### oracle — Original PC build runs headless as an oracle, frames capturable
- status: re-verified
- deps: 
- evidence: C005; tools/run_shim.sh stock -> Beenox splash frame, 1713 colours
- where: tools/run_shim.sh
- gap: 
- notes: 

### dll-swap — PE export-forwarding proxy is transparent in the running game
- status: re-verified
- deps: oracle
- evidence: C004; 53 vs 54 game-process modules, sole delta the forward target
- where: tools/pe.py proxydef
- gap: 
- notes: 

### abi — mingw code receives real MSVC __thiscall calls from the game
- status: re-verified
- deps: dll-swap
- evidence: C006; 22 asm thunks, 9 calls traced in boot order, game reached the intro cinematic
- where: tools/gen_trace.py
- gap: 
- notes: 


## recomp

### rc-decode — x86-32 decoder covering the mnemonics that actually occur
- status: re-verified
- deps: abi
- evidence: C019; 521/521 functions, 8754/8754 instructions, 0 blockers
- where: 
- gap: x87 is written but ENTIRELY UNVERIFIED (C021): 0 of 8 x87 functions are differentially verified, and random-object fuzzing cannot reach them -- the original overflows its own stack on a garbage vtable. Needs constructed valid objects. 156 of 521 functions verified overall.
- notes: 

### rc-lift — Emit C per function from Ghidra-identified boundaries
- status: re-partial
- deps: rc-decode
- evidence: src/recomp/libIGDisplay.c, 20349 lines, compiles clean with -Wall
- where: 
- gap: Emits per-function C over a CPU struct with lazy flags and a dispatch hook, but the interop layer does not exist: 163 import stubs are declared and unimplemented, and no entry shim marshals a real call into a recompiled body.
- notes: 

### rc-imports — Host implementations of the imported Win32/D3D8/DInput/CRT surface
- status: re-verified
- deps: rc-lift
- evidence: C014; 163 import stubs + 82 export shims, ESP-switch calling with callee-driven cleanup; game runs
- where: 
- gap: Stubs resolve by name at load and abort if unresolved; no host reimplementation of any Win32/D3D8 API yet -- they still call the real ones.
- notes: 

### rc-first-dll — Recompiled libIGDisplay.dll runs in the real game
- status: re-partial
- deps: rc-imports
- evidence: C020; 156 verified functions live in the game
- where: 
- gap: 156 of 748 exported entry points recompiled, 652 forwarded. 237 cases untestable by random-object fuzzing.
- notes: 

### rc-overrides — Native overrides replacing recompiled functions, A/B toggleable
- status: todo
- deps: rc-first-dll
- evidence: 
- where: 
- gap: Recomp body kept alive so each override stays diffable.
- notes: 

### rc-exe — Recompiled XMen2.exe
- status: re-partial
- deps: rc-first-dll
- evidence: C023; 11,061 of 11,106 functions translate and compile
- where: 
- gap: Compiles but has never executed: no entry shim, no host layer for its 989 imports, no differential test.
- notes: 

### rc-defect-listscan — OPEN: recompiled igTObjectList find/removeAllByValue fault where the original does not
- status: todo
- deps: rc-decode
- evidence: C022
- where: 
- gap: C022 falsified as overstated: 54 constructed combinations of find() including edge cases show ZERO mismatches, so this is probably a harness asymmetry rather than a translation bug. Still unexplained under the fuzzer and the two functions stay excluded. Next: log the actual count/base/start on a faulting trial instead of inferring.
- notes: 

### rc-modules — Recompiler generalises across modules
- status: re-partial
- deps: rc-decode
- evidence: C023; libIGDisplay/libIGAudio/libIGCollision 100%, XMen2.exe 99.6%, all compiling
- where: 
- gap: Translation only. 12 of 16 DLLs not yet imported; nothing outside libIGDisplay is differentially verified or executed.
- notes: 

### rc-exe-run — Recompiled XMen2.exe executes; stops at first untranslated indirect target
- status: re-partial
- deps: rc-exe
- evidence: C030; recompiled game reaches display init and draws its own modal dialog; 1 fallback remaining
- where: 
- gap: Display init fails because recompiled code passes garbage D3DPRESENT_PARAMETERS (C032), NOT for environmental reasons as previously concluded.
- notes: 

### rc-hybrid — Hybrid fallback: untranslated targets run original machine code
- status: hack
- deps: rc-exe-run
- evidence: 
- where: 
- gap: Down from 7 fallback addresses to 1 after feeding runtime-discovered targets back through AddFunctions.py. The loop works: run -> collect -> add -> re-export -> re-recompile. Still DEBT until it reaches 0.
- notes: 

### rc-defect-present — OPEN: recompiled code fills D3DPRESENT_PARAMETERS with garbage
- status: hack
- deps: rc-exe-run
- evidence: C032
- where: 
- gap: CONFIRMED by control: the ORIGINAL exe in the same dir with the same env produces 800x600 R5G6B5; only the executable differs. Two competing hypotheses (C033 field offset, C034 struct-by-value stack shift). Settle by tracing engine-call arguments for both exes and diffing -- not by more reasoning about the numbers.
- notes: 


## xbox

### xb-lift — Xbox XBE lifts to C and builds a native Linux executable
- status: re-verified
- deps: 
- evidence: C035/C036/C038/C049; 24,663/24,663 functions across all ELEVEN executable sections, 0 failures
- where: xbox/, vendor/xboxrecomp (patches/xboxrecomp/*.patch)
- gap: 239 jumps still deleted in 2 functions of the newly-covered sections (sub_0048447E in XGRPH, sub_0040C4E0) -- reported loudly by the translator, not yet diagnosed.
- notes: 

### xb-run — Recompiled Xbox build executes the game's main thread
- status: re-partial
- deps: xb-lift
- evidence: C048/C049/C054/C057/C058; 1050 kernel calls, 8228 indirect calls
- where: 
- gap: A factory called through vtable+0x3C on the singleton at 0x0070F7E4 returns a null object, and the next virtual call through it has no vtable. Nothing renders.
- notes: 

### xb-discovery — Runtime discovery loop for statically-invisible functions
- status: re-verified
- deps: xb-lift
- evidence: C040/C041/C054; 1311 seeds -- 23 observed at runtime, 1288 harvested from vtables in one pass
- where: tools/xbox_relift.sh, tools/xbox_discover.sh, tools/xbox_vtable_seeds.py, xbox/seeds.json
- gap: 
- notes: 

### xb-kernel — Xbox kernel bridge: ordinals bound to the right functions
- status: re-partial
- deps: xb-run
- evidence: C042/C043; validate_ordinals.py now covers the bridge dispatch and stdcall arg-size tables and reports OK
- where: 
- gap: Names validated across five tables (C043/C044). Bodies still unaudited. Volume geometry corrected to FATX 16 KB clusters (C046); ExQueryNonVolatileSetting (24) still has no bridge.
- notes: 

### xb-bounds — OPEN: function boundaries under-sized, so 6288 function tails are empty stubs
- status: re-verified
- deps: xb-lift
- evidence: C048; stub count 7998 -> 348, 0 jumps deleted, title stops rebooting
- where: 
- gap: 
- notes: 

### xb-flags — Branch conditions evaluate the flags that were actually set
- status: re-verified
- deps: xb-lift
- evidence: C050; 216 deferred cmp/jcc pairs read a reassigned operand; snapshotting takes it to 0 and removes a live-vtable corruption in the D3D static initialiser
- where: 
- gap: 
- notes: 

### xb-d3d — NV2A GPU emulation wired into the VEH
- status: re-partial
- deps: xb-run
- evidence: C053/C055; NV2A initialised and the VEH implemented on sigaction -- the GPU path exists end to end for the first time
- where: 
- gap: Not one GPU register fault has been decoded: the title dies in the CRT heap before its first draw. Whether the decoder handles this title's access patterns is untested; main.c counts and prints any it cannot decode.
- notes: 

### xb-nheap — DEBT: native CRT heap override bypassing the recompiled MSVC heap
- status: hack
- deps: xb-run
- evidence: C057
- where: 
- gap: Bypasses C056 rather than fixing it. The recompiled RtlAllocateHeap/RtlFreeHeap are still linked and run under XBOX_NATIVE_HEAP=0, so the defect stays reproducible. RtlReAllocateHeap and RtlSizeHeap are not overridden -- a realloc of a native-arena pointer would corrupt it undetected.
- notes: 

