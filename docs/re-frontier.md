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
- status: todo
- deps: ark
- evidence: 
- where: tools/ghidra_scripts/DumpVtab.py
- gap: Cheaper than assumed (C009): only the vtable POINTER must be supplied, via retrieveVTablePointer, so MSVC layout need not be reproduced. What remains is slot ORDER within the vtable, which virtual callers index by. Read it with DumpVtab.py.
- notes: 

### constructderived — igObject::constructDerived -- how libIGCore finishes an object
- status: todo
- deps: ark
- evidence: 
- where: 
- gap: Where the captured vtable pointer is stamped and per-class construction runs. Needed before our own class can be handed to libIGCore.
- notes: 


## input

### ctrlmgr — Native igControllerManager / igWin32ControllerManager
- status: todo
- deps: vtable
- evidence: 
- where: 
- gap: Replacing behaviour requires owning construction (C007); intercepting exports is not enough.
- notes: 

### sdl-input — SDL_GameController backend + the three shipped features
- status: todo
- deps: ctrlmgr
- evidence: 
- where: 
- gap: Hotswap / auto-mapping / Xbox prompts all land here.
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

