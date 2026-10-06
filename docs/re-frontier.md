# RE Frontier — the ordered RE dependency chain toward faithful X-Men 2 execution

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

**Fail fast & loud:** a failure must surface loudly. The CPU runtime may use
only its explicit, counted bounded fallback for failed or unsupported JIT
compilation or unsafe execution; fallback-backed intervals cannot establish
gameplay or performance evidence. Other silent fallbacks remain forbidden
unless the fallback is intended behavior of the real target being reproduced.

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
- gap: Retained guest construction preserves the original vtables. Re-open only if a native override must construct an Alchemy object itself.
- notes:

### constructderived — igObject::constructDerived -- how libIGCore finishes an object
- status: skip-by-design
- deps: ark
- evidence:
- where:
- gap: Retained guest construction owns this path; it is needed only when a native override constructs objects itself.
- notes:

### cutscene-player — Port the in-game cutscene player above conversations
- status: re-partial
- deps: native-overrides
- evidence: XMen2.exe 004d9640/004d8b30 BehavEd scheduler/context player; 004b2b40 insertion and 004b2d70 timed-event player; 00458700 response-voice, 0045a170 line-voice, and 004a7130 BehavEd sound presenters; C247/C263/C274; test_behaved_context; test_behaved_player_heap; test_cutscene_event_player; test_cutscene_dialogue; test_cutscene_script_audio; visible 11/11 and camera-only 10/10 live gates with zero dialogue leaks and both authored sound commands silent
- where: src/native/cutscene_player.c; src/native/behaved_context.c; src/native/behaved_player.c; src/native/cutscene_event_player.c; src/native/cutscene_dialogue.c; src/native/cutscene_script_audio.c; docs/RE/cutscene_player.md
- gap: The tutorial control-lock epoch is verified end to end. Other maps may compose additional local players or branching payloads and must refuse until their binary ownership is recovered; no global world update or clock advance is an allowed fallback.
- notes: Ordinary pumps retain strict deadline<now. The native 004d8b30 context interpreter is shared by ordinary and synchronous scheduling. Exact skip steps only insertion-tagged script events and BehavEd contexts inherited from owned script, event-callback, or deterministic-payload scopes; the epoch alone does not adopt work. Conversation is a deterministic payload. The synchronous player stops the active voice and suppresses the two RE-grounded dialogue presenters plus the exact BehavEd sound handler for its owned current context; DirectSound remains ordinary. CHud's separate briefing/caption cinematic surface is not the gameplay control-return owner.

## input

### ctrlmgr — Shared Alchemy controller adapter
- status: in-progress
- deps: native-overrides
- evidence:
- where:
- gap: Qualify the shared Alchemy snapshot against the retained DirectInput publication through the title adapter before changing ownership.
- notes:

### sdl-input — The game's input system, on SDL3
- status: re-partial
- deps: native-overrides
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
- evidence: C005; tools/run_shim.py stock -> Beenox splash frame, 1713 colours
- where: tools/run_shim.py
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
- evidence: A proxy boundary run received nine retail `__thiscall` calls in boot order and reached the intro cinematic; the current proxy build is Python-driven.
- where: tools/build_shim.py; tools/proxy_d3d8/
- gap:
- notes:

## runtime

### runtime-jit — Product guest execution through shared x86port
- status: in-progress
- deps: abi
- evidence: `tools/runtime_boundary.py --source` and `--binary build/native/x2native` prove that the gameplay source and linked product use `x86port_runtime` and expose no interpreter selector or verification mode. A real bounded run against x86port `ca52e377040d83534f9cd9f5a976526078fa2343` maps all twenty retail images, passes the six-instruction shipping selftest, enters guest startup, reaches 356 tracked title/host crossings, executes the shared memory-form `FCOMP m64` emitter at mapped guest `0x103c05ec`, `FNSTSW AX` at `0x103c05ef`, and `FNCLEX` at `0x103b9fed`, then refuses `PUSHFD` (`9c`) at `0x103c30d2`.
- where: src/native/x86_engine.c; src/native/x86_dispatch.c; shared/x86port
- gap: Implement and differentially verify the reached `PUSHFD` flag-stack emitter in shared/x86port, then continue the bounded product run until representative gameplay. If shared/x86port gains the permitted bounded fallback, the product audit must prove that only failed/unsupported compilation or unsafe execution can enter it, every interval is counted, and no fallback-backed interval is used as gameplay or performance evidence.
- notes: Native imports and overrides hand back through the title dispatcher; all other guest addresses enter the runtime JIT.

### native-overrides — Incremental native subsystem ownership
- status: re-partial
- deps: runtime-jit
- evidence: C156/C178/C209; module-qualified overrides, native host services, and scoped original-body calls have reached menu, movies, gameplay, death, and return-to-menu paths.
- where: src/native/
- gap: Representative product conformance and remaining subsystem-by-subsystem ownership are incomplete.
- notes: Overrides may call original guest behavior through the same JIT without recursion.

## graphics

### vk-substitute — Vulkan renderer substituted into the engine through ARK
- status: re-verified
- deps: native-overrides
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
- deps: native-overrides
- evidence: C129/I039 establish the import/ABI cut; C156 closes the menu-to-gameplay-to-menu loop with zero refused draws; C170 verifies the title's required fixed-function combiner behavior; C173 verifies the observed VS 1.1 skinning path; issue #62 verifies model-space and XYZRHW culling by pixels.
- where: src/d3d8/, tools/d3d8_abi_check.py
- gap: The live D3D8 route renders the game loop with zero refused draws on the measured path. Cube sampling and title-used texture-coordinate generation are implemented. The earlier 0-of-298037 stage-beyond-zero result was route-local: Dead Zone water falsified C154 and now exercises its evidenced MODULATE/MODULATE stage 1, camera-normal texgen, COUNT2 transform, animated stage-0 transforms and mip chain. The observed SELECTARG1, SELECTARG2, MODULATE and ADD combiners, material color sources, conditional normal normalization, and VS 1.1 skinning run. Honest remaining gaps stay fail-loud: nonzero pixel shaders, unobserved combiner or VS-token forms, engine off-screen render targets, and incomplete fixed-function specular and spot-cone semantics. Fog was disabled in the measured black-gameplay route, not proved generally irrelevant.
- notes: This is the live renderer path and supersedes vk-frame's ARK substitution. The engine installs the host device itself after Direct3DCreate8; src/gpu is the shared backend. C177 verifies swapchain teardown ordering.

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
