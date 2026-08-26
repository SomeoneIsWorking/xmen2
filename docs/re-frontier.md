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

### cutscene-player — Port the in-game cutscene player above conversations
- status: re-partial
- deps: rc-overrides
- evidence: XMen2.exe 004d9640/004d8b30 BehavEd player; 004b2b40 insertion and 004b2d70 timed-event player; C247/C263; test_behaved_player_heap; test_cutscene_event_player; visible 9/9 and camera-only 8/8 live gates
- where: src/native/cutscene_player.c; src/native/behaved_player.c; src/native/cutscene_event_player.c; docs/RE/cutscene_player.md
- gap: The tutorial control-lock epoch is verified end to end. Other maps may compose additional local players or branching payloads and must refuse until their binary ownership is recovered; no global world update or clock advance is an allowed fallback.
- notes: Ordinary pumps retain strict deadline<now. Exact skip steps only insertion-tagged script events and BehavEd contexts inherited from owned script, event-callback, or deterministic-payload scopes; the epoch alone does not adopt work. Conversation is a deterministic payload.

## input

### ctrlmgr — Native igControllerManager / igWin32ControllerManager
- status: skip-by-design
- deps: vtable
- evidence:
- where:
- gap: Superseded: under recomp+overrides the controller manager is replaced as an override on recompiled functions, not as a hand-built class registered with libIGCore.
- notes:

### sdl-input — The game's input system, on SDL3
- status: re-partial
- deps: rc-overrides
- evidence: DirectInput 7/8 are implemented over SDL3 and driven end to end. Issue #82 fixed background button delivery; C215 publishes bindings into the master, working and menu banks the game actually evaluates; C222/C224 prove full-scale triggers and RT+A power casting; C227 proves the RB health-item row. Keyboard and synthetic-pad input reach gameplay through the shipping x2ctl.py probe.
- where: `src/native/dinput*.c`, `src/input/player_input.c`, `tools/x2ctl.py`
- gap: The synthetic pad verifies enumeration, axes, buttons, triggers, action publication, hotswap source switching and gameplay input. No physical controller has been attached on this machine, so real-device hotplug, stable identity and reconnect behavior still require hardware validation; do not promote this step to re-verified from synthetic evidence alone.
- notes: The host owns SDL/DirectInput transport in `src/native/dinput*.c`; player assignment and binding publication live in `src/input/player_input.c`. The shared Alchemy controller abstraction remains in the alchemy repository.

### xbox-defaults — Recover and port the Xbox build's controller defaults into the PC mapping UI
- status: re-partial
- deps:
- evidence: C160 proves the PC game opens/reads a pad; C184 parses the Xbox options package; C187/C227 record the 22 verified assignments. Xbox `sub_00162240` directly associates each d-pad name with NEXT/PREV/INC_AGGR/DEC_AGGR; PC `FUN_00619c40`, `FUN_0061b030`, and `FUN_006281f0` recover the action rows, 42-row object, and physical codes. The retained PC defaults and shipped PS2 potion tutorial establish `TargetLock` as the health-item control; the Xbox options package assigns health to Black, whose modern position is RB. Live input reports row 10 as `pad3:0x1a` and RB drives player physical action slot 13 to `+1.000`. `test_xbox_defaults` pins every tuple and `test_player_input` proves the sole publisher writes them to the assigned player's master, working, and menu sets. C225 corrects the former Black/White float-index model: the Xbox poller writes only four axes to its 30-float array and carries buttons in a separate digital mask. `tools/xbe_query.py` (I053) now distinguishes class-agnostic slot scans from accessor-preserving `chain`/`aftercall` queries and selftests both answers.
- where: docs/features/controller-mapping-defaults.md; src/native/xbox_defaults.c; src/input/player_input.c; tests/test_xbox_defaults.c; tests/test_player_input.c; tools/extract_fb.py; Xbox default.xbe and controller-options FB
- gap: Identify the retained PC action that corresponds to Xbox White / Use Energy Pack, add it only when the PC and console evidence join as they do for health, and validate device identity/assignment on physical hardware.
- notes: Health is implemented through `TargetLock`; no native inventory or healing behavior was invented. Energy remains deliberately omitted rather than aliased to `QuickPower`.

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
- evidence: C179; tools/ghidra_export.sh lift_step_guard selftest covers silent/Jython-failed/large-output steps plus matching, mismatched and unknown source provenance; current libIGSg stamp matches the installed PE and verify_export reports 5 agreeing sections, 4714 functions, 0 truncated
- where: tools/ghidra_export.sh, tools/verify_export.py, tools/recomp.py emit, src/recomp/
- gap: Real per-function lift is live across the exe and all game modules. Remaining decoder gap is the untaken 3DNow path plus AAA/DAA/BOUND/XLAT/ENTER inside two regions Ghidra decoded from embedded data; those stay fail-loud rather than translating noise. The standing rc-hybrid fallback path remains separate debt.
- notes:

### rc-imports — Host implementations of the imported Win32/D3D8/DInput/CRT surface
- status: re-verified
- deps: rc-lift
- evidence: C014/C176; the native run binds imports across every recompiled module, resolves run-time-loaded DInput8 and DirectSound exports by name, and reaches gameplay with 0 unresolved-call stops. DirectSound evidence: XMen2.exe FUN_00594290/FUN_00594590/FUN_00594e50/FUN_00596050 plus a frame-2900 run with 270 secondary buffers, 18 duplicates and 1,249,706 nonzero mixed samples.
- where:
- gap: The reached Win32/D3D8/DInput/DirectSound/CRT surface is native and fail-loud. Unreached imports remain poison thunks by design until a real route demands them; SEH delivery, LAN sockets, and several optional COM/system facilities are still absent and named in the codemap.
- notes:

### rc-first-dll — Recompiled libIGDisplay.dll runs in the real game
- status: re-partial
- deps: rc-imports
- evidence: C080; C020; ALL 521 translatable functions recompiled and the game renders (scratch/screenshots/all.png, 2307 colours, zero fault blocks)
- where:
- gap: The blocker was NOT which functions to recompile -- it was the entry path (C080): the runtime kept its own state below the guest stack pointer, so the guest and every host callee overwrote it, and any recompiled function that ran a host call died. With a private runtime stack the FULL translatable set runs. What is left is coverage and faithfulness, not survival: 150 of the 748 exported names are still forwarded (514 shims cover the other 598), and the recompiled bodies that DO run are few (17 entries over 9 entry points in a 30s intro run) -- so most of them remain unexercised rather than verified.
- notes:

### rc-overrides — Native overrides replacing recompiled functions, A/B toggleable
- status: re-partial
- deps: rc-first-dll
- evidence: xbox/overrides.json; vendor/xboxrecomp/tools/recomp (--isolate); xbox/src/recomp/gen/recomp_overrides.cmake; scratch/logs/xbox_run_iso.log isolation self-test
- where:
- gap: The MECHANISM is now sound rather than lucky (issue #4, I015): xbox/overrides.json is the single source of truth, recomp --isolate gives every overridden function its own translation unit so -Wl,--wrap always binds, the lift generates the wrap flags from the same file and exits non-zero if an override was not isolated, and CMake refuses to build without the generated list. Proven on both classes -- the wrapper that measurably never fired now fires, and it is kept as the standing regression test. What is still missing is coverage: only 3 real overrides exist (the heap trio) plus 3 observers. The faithfulness work has not started.
- notes:

### rc-exe — Recompiled XMen2.exe
- status: re-partial
- deps: rc-first-dll
- evidence: C023; 11,061 of 11,106 functions translate and compile
- where:
- gap: Compiles but has never executed: no entry shim, no host layer for its 989 imports, no differential test.
- notes:

### rc-defect-listscan — OPEN: recompiled igTObjectList find/removeAllByValue fault where the original does not
- status: re-verified
- deps: rc-decode
- evidence: C074; C075; issue #5; scratch/logs/xbox_run_cf.log (0 implausible memcpy, run continues past the old blocker)
- where:
- gap:
- notes: CLOSED 2026-08-05. Root cause was not this list at all: the recompiler never set the carry flag, so MSVC's sbb-sign strcmp idiom always answered greater and the name-table binary search could not find keys that were present (C075, issue #5). With CF materialised the ~4GB tail-shift memcpy is gone from the boot -- 0 implausible copies where there was reliably 1 -- and the run proceeds past it into new code. The list remove and its caller were faithful all along (C074).

### rc-modules — Recompiler generalises across modules
- status: re-partial
- deps: rc-decode
- evidence: C023; libIGDisplay/libIGAudio/libIGCollision 100%, XMen2.exe 99.6%, all compiling
- where:
- gap: Translation only. 12 of 16 DLLs not yet imported; nothing outside libIGDisplay is differentially verified or executed.
- notes:

### rc-exe-run — Recompiled XMen2.exe runs through renderer startup and the Activision intro
- status: re-partial
- deps: rc-exe
- evidence: C027; C180; C181; issue #68
- where:
- gap: The Wine-hosted x2run reaches ResetSwapChain, display-mode setup, Cg loading, renderer state setup and the Activision intro with stock-matching presentation parameters. It is not verified through the whole game loop; the next work on this track is the existing driven end-to-end discriminator. Native gameplay-loop evidence belongs to rc-native and d3d8-host, not to this Wine-hybrid step.
- notes: The former title saying this step stopped at the first untranslated indirect target was stale; that discovery frontier has moved past startup.

### rc-hybrid — Hybrid fallback: untranslated targets run original machine code
- status: hack
- deps: rc-exe-run
- evidence: C077; issue #8; scratch/logs/xbox_run_bounds.log (0 ABI violations, 0 empty-stub calls, the NULL indirect call gone)
- where:
- gap: NO LONGER the blocker, and the register file is clean: 94088 checked calls all restore ebx/esi/edi/ebp, and none of the 168 empty stubs is called (C077, issue #8). Still DEBT only in that the fallback path exists. The PC native --d3d8 run is now past every translator and discovery stop: rotates (ROL/ROR/RCL/RCR) are translated and checked against the host CPU's own instructions (tests/test_rotate.c, 5377 checks -- RCR is on the path of any guest that divides a long long, via MSVC's __allrem), and XMen2.exe's indirect-call targets are bulk-seeded from data pointers (tools/seed_data_ptrs.py), so the discovery loop converges in ONE round instead of grinding out one function per round. A 240-second run presents 3769 frames and 380289 draws with no stop of any kind -- it ended on the timeout, not on a defect.
- notes:

### rc-defect-present — CLOSED: current recompiled code fills D3DPRESENT_PARAMETERS identically to stock
- status: re-verified
- deps: rc-exe-run
- evidence: C180
- where:
- gap:
- notes: CLOSED 2026-08-14. Fresh current x2run and stock Wine runs each emitted exactly one ResetSwapChain block; all fields matched: 800x600, R5G6B5, D16, fullscreen, swap effect 1. C032 was real historical evidence but is falsified for the current translator; C033/C034 were unproven hypotheses. tools/build_x2run.sh now makes the discriminator reproducible.

### rc-native — The PC recomp produces an artefact that runs WITHOUT Wine
- status: re-partial
- deps: rc-exe-run
- evidence: C081/C156/C178/C209; x2native's battery checks stdcall/cdecl callback cleanup, a deliberate zero-cleanup mutation aborts, and the native D3D8 route has completed menu, movies, level load, gameplay, death dialog and return to menu with zero refused draws.
- where: src/native/, tools/recomp.py native, CMakeLists.txt target x2native
- gap: The native x86-64 ELF and arm64 Mach-O now map and initialise the recompiled exe and game modules, supply the reached Win32, DirectInput, DirectSound and D3D8 host surfaces, and run the game loop. Apple Silicon uses a translated 4 GB guest arena under the normal Mach-O `__PAGEZERO` (issue #10 closed). Remaining work is coverage and faithfulness: unreached imports remain fail-loud poison thunks; guest exception delivery, LAN sockets and optional COM/system facilities are still absent.
- notes: x2native composes the generated recompiled bodies with src/native, src/d3d8 and src/gpu. The former note claiming the native CMake build was unrelated to the recomp was retired as stale.

### rc-modinit — Native module initialisation: nothing runs DllMain or the CRT per module
- status: re-verified
- deps: rc-native
- evidence: x2native output: both modules run their PE entry point (DllMainCRTStartup) and DllMain returns TRUE; libIGCore's 51 static constructors execute
- where: src/native/x2native.c (modules_init), src/native/win32_sdl.c (_initterm, __dllonexit), tools/ghidra_export.sh --seed
- gap: DONE. Modules initialise in dependency order read from their import tables, on the guest stack, with FS:[0] backed by a real TIB word for the SEH prologues. Two things were needed: the 51 static-constructor targets are referenced ONLY by a data pointer in .rdata, so Ghidra never marked them as code -- _initterm now enumerates every missing target in one pass and ghidra_export.sh --seed feeds them back (5818 -> 5918 functions); and MSVCRT __dllonexit is implemented for real rather than stubbed, because a stub returning func looks identical while dropping every registration. NOT covered: exception DELIVERY. The SEH chain is kept well-formed but nothing walks it, so a guest exception would go nowhere.
- notes: Evidence: gdb backtrace on x2native shows the transfer working and the fault landing inside libIGCore's own body, not in the dispatch path.

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
- evidence: C048/C049/C054/C057/C058/C060/C061/C070/C071/C072; the registry NULL that stood here is gone -- it was a symptom of our own NtAllocateVirtualMemory ignoring a placed base (C070), and of an esp-relative indirect-call target read four bytes low (C072)
- where:
- gap: Still nothing renders. The run now reaches the runtime-discovery loop's territory again: unresolved indirect calls into functions the static detector cannot see. C062's "initialisation order" diagnosis was FALSIFIED -- do not re-derive it.
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
- gap: Bypasses C056 rather than fixing it. The recompiled RtlAllocateHeap/RtlFreeHeap are still linked and run under XBOX_NATIVE_HEAP=0, so the defect stays reproducible. RtlSizeHeap is not overridden. The --wrap bypass recorded here on 2026-08-05 is FIXED (issue #4): recomp --isolate now emits each overridden function into its own translation unit, so all 11 RtlAllocateHeap call sites reach the override, not 9 of 11.
- notes:

## graphics

### vk-substitute — Vulkan renderer substituted into the engine through ARK
- status: re-verified
- deps: rc-native
- evidence: C115/C117/C118/C120/C121. igVkVisualContext is a HOST-defined class registered with ARK as a real subclass of igDx8VisualContext, so libIGCore constructs it with the engine's own Dx constructor chain and stamps our vtable. Running --vk, the engine NEVER calls Direct3DCreate8 (C118) and the engine's own code creates a real Vulkan device via SDL_CreateGPUDevice (C120, backend reported as "vulkan"). The vtable is seeded from igDx8VisualContext -- 334 slots inherited -- with the 98 device-touching ones generated by tools/device_slots.py (I036) overridden. This step is the SUBSTITUTION MECHANISM only, verified on real runs; it does not claim anything is drawn.
- where: src/vulkan/
- gap:
- notes:

### vk-frame — A frame reaches the screen: the engine's beginDraw/clear/endDraw drives a Vulkan present
- status: re-partial
- deps: vk-substitute
- evidence: The engine's frame boundary is RE'd from its own bodies: beginDraw at libIGGfx 0x1002eb30 is getLastError-then-BeginScene, endDraw at 0x1002eb70 is EndScene + Present with a D3DERR_DEVICELOST check and a 64-bit frame counter at 0x101895b0, clearRenderDestination at 0x1002ee90 reads its colour from this+0x190..0x19c and its flags from a byte mask, setViewport at 0x1002ec70 clamps against the render destination and is super-called. All four slots implemented against SDL_GPU. THE HOST HALF IS VERIFIED TO PRESENT: x2native --vk-selftest, wired in as the vk_frame_path ctest, drives acquire/clear/present with no engine and reports '3 frame(s) presented, 0 skipped'. It is a real discriminator -- it FAILED on its first run (0 of 3, because the window was created hidden and so had no swapchain image) before the setup was corrected.
- where: src/vulkan/igvk_device.c, src/vulkan/igvk_slots_frame.c
- gap: 0 frames presented, and --vk-permissive now shows exactly how far the engine gets: 34 distinct render-state slots deep, stopping in Gap::Gfx::igDx8DecalExt::setDecalOffset -- which is NOT a slot of igVisualContext's vtable. It belongs to a DIFFERENT ARK class.
- notes: SUPERSEDED as the live renderer path by d3d8-host, and the reason is measured (C129) rather than a preference. The gap above said the remaining work was either a host IDirect3DDevice8 or nine more ARK substitutions; the host device turned out not to need the ARK substitution AT ALL, because the engine builds its own igDx8VisualContext and installs the device itself once Direct3DCreate8 answers. src/vulkan/ is kept and still runs under --vk: it is the only thing that has driven the engine's frame boundary end to end, and its GPU half is now src/gpu/, which the D3D8 device draws through.

### d3d8-host — Host Direct3D 8: the engine's own DirectX code runs, answered at the Direct3DCreate8 import
- status: re-partial
- deps: rc-native
- evidence: C129/I039 establish the import/ABI cut; C156 closes the menu-to-gameplay-to-menu loop with zero refused draws; C170 verifies the title's required fixed-function combiner behavior; C173 verifies the observed VS 1.1 skinning path; issue #62 verifies model-space and XYZRHW culling by pixels.
- where: src/d3d8/, tools/d3d8_abi_check.py
- gap: The live D3D8 route renders the game loop with zero refused draws on the measured path. Cube sampling and title-used texture-coordinate generation are implemented; 0 of 298037 measured draws used a stage beyond stage 0, so multitexture is not a current title gap; the observed SELECTARG1, SELECTARG2, MODULATE and ADD combiners and VS 1.1 skinning run. Honest remaining gaps stay fail-loud: nonzero pixel shaders, unobserved combiner or VS-token forms, engine off-screen render targets, and incomplete fixed-function specular, spot-cone and MATERIALSOURCE semantics. Fog was disabled in the measured black-gameplay route, not proved generally irrelevant.
- notes: This is the live renderer path and supersedes vk-frame's ARK substitution. The engine installs the host device itself after Direct3DCreate8; src/gpu is the shared backend. C177 verifies swapchain teardown ordering.

## rc-native

### crt-setjmp — setjmp/longjmp across recompiled frames
- status: re-verified
- deps: rc-native
- evidence: REAL MECHANISM, not a stopgap. tools/recomp.py detects the one-instruction JMP-through-IAT thunk that forwards to _setjmp3 and emits, at every DIRECT CALL to it, an inline host setjmp in the CALLING body -- which is the only frame a host longjmp may resume into. x86_setjmp_buf snapshots the guest register file against the guest's own jmp_buf pointer; x86_setjmp_done finishes the call either way, restoring the snapshot (ESP included) when a longjmp arrives. 31 inline setjmps emitted across XMen2, libIGGfx and libIGLua. VERIFIED ON A REAL RUN, not by inspection: 'crt: longjmp RESUMED into a generated body (rc=1, guest esp restored to 0x700ff598)' -- and the run continues past it, where before it stopped there. The thunk-detection step was the part that was initially missing and it failed SILENTLY: MSVC routes the call through a thunk, so the emitter never saw a call to the import and emitted 0 inline setjmps while reporting success.
- where: src/native/crt.c
- gap: The import stub form remains for a call that reaches _setjmp3 indirectly rather than through generated code. It records the buffer as UNRESUMABLE and longjmp refuses by name if one ever arrives there, so the case cannot pass silently -- but it is not implemented. Also unhandled: host-side state owned by frames the longjmp destroys (the ark scratch-stack pointer, for one) is not unwound.
- notes:

## renderer

### pc-vs11 — D3D8 VS 1.1 lifecycle and execution
- status: re-verified
- deps:
- evidence: libIGGfx 0x10048500 calls the real D3D8 CreateVertexShader; scratch/logs/create-vs-dump.log captures the five-token declaration and 104-DWORD VS 1.1 program. scratch/logs/vs-execute-3350.log records 50 programmable draws / 3250 vertex invocations through frame 3350 with zero GPU refusals; d3d8_vs_selftest proves relative DP4 execution and unsupported-opcode refusal.
- where: src/d3d8/d3d8_vertex_shader.c; src/d3d8/d3d8_device.c; src/d3d8/d3d8_drawcall.c
- gap: Observed program is implemented and verified; any unobserved declaration form, modifier, or VS 1.1 opcode still refuses by token until reached and implemented.
- notes: The interpreter preserves the engine program and constants. It does not map programmable shaders back to fixed-function FVF.

### native-ui-prompts — Native Alchemy text slice renders port-owned SVG prompts
- status: re-verified
- deps: d3d8-host
- evidence: C271; XMen2 FUN_005ee780 preflights each string and FUN_005ee400 retains the retail emitter/finalizer event with collapsed geometry; libIGGfx igDxVisualContext::drawNonIndexed 0x100352d0 brackets the batch and nested updateContextState 0x10034e60 finalizes it before DrawPrimitive; computeMatrix_Dx 0x1003ec10 publishes context-keyed engine W/V/P. scratch/logs/svg-final-unbounded.log plus scratch/screenshots/svg-final-unbounded.png verify the ENTER keycap. scratch/logs/svg-pad-final-unbounded.log records 1,073 pure one-codepoint controller strings through 1,073 matching nested finalizers/submissions with zero atomicity, transform, GPU or orphan refusals; scratch/screenshots/svg-pad-unbounded.png shows the aligned A icon.
- where: src/native/prompt_glyph_draw.c; src/native/prompt_glyph_batch.c; src/native/ui_transform.c; src/gpu/gpu_prompt_glyphs.c; docs/RE/text.md
- gap:
- notes: This is the first native 2D/UI draw slice. It bypasses D3D8 for port-owned prompt art but retains the stock ASCII batch; it does not claim the rest of Alchemy 2D is ported.

### native-ui-rest — Port remaining Alchemy 2D/UI draw types and retire their D3D8 calls
- status: todo
- deps: native-ui-prompts
- evidence:
- where: src/native/; src/gpu/; src/d3d8/; docs/strategy.md
- gap: Only prompt SVG quads are native. Stock text, panels, sprites, and other display-list geometry still submit through the D3D8 seam; each semantic path must be REd and verified before its D3D calls can be removed.
- notes: Proceed top-down from display-list/geometry attributes; do not grow a D3D draw classifier.
