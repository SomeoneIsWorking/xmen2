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
- evidence: C012/C013; 82 functions match the original over 292,700 trials, negative control fires
- where: 
- gap: x87 (FILD/FLD), SBB and REP string ops still untranslatable: 21 of 521 functions. Verification covers only call-free, write-free functions comparing EAX.
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
- evidence: C014; hybrid DLL loads and the game reaches the intro cinematic
- where: 
- gap: 82 of 748 exported entry points recompiled, 745 exports still forwarded. The all-500 build page-faults and needs bisection.
- notes: 

### rc-overrides — Native overrides replacing recompiled functions, A/B toggleable
- status: todo
- deps: rc-first-dll
- evidence: 
- where: 
- gap: Recomp body kept alive so each override stays diffable.
- notes: 

### rc-exe — Recompiled XMen2.exe
- status: todo
- deps: rc-first-dll
- evidence: 
- where: 
- gap: 11,106 functions, 77.5% byte coverage, 643,647 instructions. The eventual target.
- notes: 

