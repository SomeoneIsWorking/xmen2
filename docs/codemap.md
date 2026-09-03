# Codemap

The single-page answer to **which subsystem owns each responsibility, where it
lives, and where related work should go**. Capability state belongs in
`docs/project-state.md`; epic intent belongs in `docs/project-goals.md`; atomic
work belongs in `docs/issues/`; proof and tool trust belong in `docs/info/`;
ordered reverse-engineering dependencies belong in `docs/re-frontier.md`.

## Architecture

```text
user PE images + committed Ghidra exports
                  |
                  v
     bootstrap/provision/recompiler tools
                  |
                  v
       generated guest C in src/recomp
                  |
                  v
 PE mapper + x86 runtime + subsystem-owned native overrides
                  |
          +-------+-------+
          |               |
          v               v
 host Win32 services   title/engine bridges
          |               |
          +-------+-------+
                  |
                  v
          host D3D8 translation
                  |
                  v
     shared SDL_GPU renderer and presenter
                  |
                  v
       SDL window/input/audio boundary
```

Configuration, input, saves, presentation, media/audio, and RmlUi are cohesive
host owners composed at the native boundary. Asset-format and reusable Alchemy
engine code lives in the separate `alchemy` repository and is resolved through
`tools/alchemy_path.py`; this port keeps only title-specific bridges and tools.

## Source ownership

The directory rows establish the broad ownership boundary. The detailed rows
below name cross-directory seams and the entry points used to extend them.

| Subsystem | Responsibility | Location | Entry point | Deep doc |
|---|---|---|---|---|
| Title runner | Title-specific asset-viewer composition | `src/app/` | `x2run.c` | — |
| Movie audio | Streaming movie voice and DirectSound-facing audio sink | `src/audio/` | `movie_audio.c` | [FMV](RE/fmv.md) |
| Gameplay cutscene player | Own BehavEd command/scheduler and timed-event steps through authored control release; silence exact dialogue and script-audio presenters only during synchronous completion | `src/native/cutscene_player.c`, `src/native/behaved_context.c`, `src/native/behaved_player.c`, `src/native/cutscene_event_player.c`, `src/native/cutscene_dialogue.c`, `src/native/cutscene_script_audio.c` | `x2_cutscene_player_finish`, `behaved_context_run` | [Cutscene player](RE/cutscene_player.md) |
| Persistent configuration | Settings schema, OS user configuration directory, storage, boot mode, and controller assignments | `src/config/` | `config_directory.c`, `settings.c`, `settings_store.c` | [Co-op participation](RE/co_op_participation.md) |
| Runtime CVars | Register the port's engine/JIT knobs and resolve them through layers (compiled default < `x2native-runtime.conf` < `X2_*` env < `--set NAME=VALUE`); the `lucent::cvar` library owns the layering, this file owns the definitions and the config-file location | `src/config/runtime_cvars.{cpp,h}` | `x2_runtime_config_init`; `lucent_cvar_text("engine")` from `x86_engine.c` | — |
| Direct3D 8 host | COM ABI, resources/formats, state, two-stage texture lowering, fixed/programmable vertex processing, and renderer diagnostics | `src/d3d8/`, `src/gpu/gpu_texture_format.c` | `d3d8_d3d8.c`, `d3d8_device.c`, `d3d8_drawcall.c`, `d3d8_texture_stage.c` | [Shadows](RE/shadows.md) |
| D3D8 texture provenance | Own immutable runtime texture metadata plus successful 2D base-level content fingerprint/revision, without asset-name claims | `src/d3d8/d3d8_texture_provenance.{c,h}` | initialized and updated by `d3d8_resource.c`, snapshotted into `D3D8DrawRequest` | — |
| D3D8 selector evidence | Passively correlate an exact texture or untextured draw class with pre-build geometry, accepted/refused lowering results, and world-matrix ancestry, without asserting identity the evidence does not establish | `src/d3d8/d3d8_selector_probe.{c,h}`, `src/d3d8/d3d8_selector_probe_json.{c,h}`, `src/d3d8/d3d8_selector_probe_bridge.c`, `tools/selector_probe.py` | `d3d8_selector_probe_build_request`, `tools/selector_probe.py` | — |
| Host GPU | SDL_GPU device, deterministic frame initialization, uploads, draws, capture, composition, frame submission, bounded exact frame timing, prompt glyphs, and shadow pass | `src/gpu/` | `gpu_device.c`, `gpu_draw.c`, `gpu_frame_timing.{c,h}`, `gpu_frame_timing_report.{c,h}` | [Text and prompts](RE/text.md), [Shadows](RE/shadows.md) |
| Input policy | Binding rows, device identity/admission, player association, participation, probing lifecycle, exact recording, and authored virtual touch action mapping | `src/input/` | `player_input.c`, `input_record.c`, `touch_controls.cpp` | [Co-op participation](RE/co_op_participation.md), [Android release](android-release.md) |
| Media decode | SFD demux, video decode, ADX decode, timing, drain, and probe policy | `src/media/` | `fmv_player.c` | [FMV](RE/fmv.md) |
| Native host and bridges | PE/runtime composition, POSIX Win32 services, title and engine overrides, live control, boot, input publication, and diagnostics; `engine_file_path.c` bridges the retail registry-backed file search to virtual `C:\`; fatal signals report a symbolizable host PC before the guest-state ring | `src/native/` | `x2native.c`, `engine_file_path.c`, `fault_report.c`, `x86rt_native.c` | [Boot](RE/boot.md) |
| Guest CRT bulk-write diagnostics | Publish native `memcpy`/`memmove`/`memset`/string writes to the shared guest write-watch instrument | `src/native/crt_write_watch.{c,h}` | `crt_write_watch_dst` | [Instrument I062](info/instruments/062-x2-write-watch-one-shot-on-a-reused-stack-slot.md) |
| Oracle records | Shared binary record ABI for stock-proxy and native traces | `src/oracle/` | `probe_rec.h` | — |
| Presentation policy | Window settings, transactional live logical-resolution changes including retained title display state, aspect fit, display-mode publication, and boot blackout | `src/presentation/`, `src/native/display_mode_runtime.{c,h}` | `x2_live_resolution_apply`, `x2_display_mode_runtime_apply`, `x2_override_display_settings_load` | — |
| Retail dialog selected-row scale | Own the shared 800x600 retail UI reference and extend the title's evidenced selected-row transform formula above that reference at its exact call site | `src/presentation/retail_ui_design.h`, `src/native/dialog_selection_scale_policy.{c,h}`, `src/native/dialog_selection_scale.{c,h}` | `x2_dialog_selection_scale`, `x2_dialog_selection_transform` | [Issue #133](issues/0133-retail-dialog-selection-highlight-is-missing.md) |
| Generated guest runtime | Hand-written x86 runtime headers plus generated module bodies and dispatch tables | `src/recomp/` | `x86rt.h`; output from `tools/recomp.py` | — |
| Save model | Retail save directory, catalog, Continue policy, autosave policy/format/storage, Load Game logical-window policy, and trace model | `src/save/` | `save_directory.c`, `save_catalog.c`, `load_game_menu_policy.c` | [Boot](RE/boot.md) |
| Port settings UI | RmlUi lifetime, settings document, controller rows, and overlay state | `src/ui/` | `rmlui_ui.cpp`, `settings_document.cpp` | [Prior art](prior-art.md) |
| ARK visual-context backend | Alchemy visual-context class construction and slot bridges | `src/vulkan/` | `igvk_context.c`, `igvk_ark.c` | — |

## Cross-directory subsystem seams

| Subsystem | Responsibility | Location | Entry point | Deep doc |
|---|---|---|---|---|
| Bootstrap and launcher | Validate user images, provision pinned inputs, regenerate derived code/assets, configure, build, and launch the product | `bootstrap.py`, `tools/provision.py`, `tools/run.py`, `run.sh` | `bootstrap.py:main`, `tools/run.py:main` | — |
| Binary export and discovery | Export Ghidra functions/instructions and recover statically hidden entry points | `tools/ghidra_scripts/`, `tools/ghidra_scripts_local/`, `tools/native_discover.py`, `tools/seed_relocs.py`, `tools/seed_data_ptrs.py`, `tools/seed_code_imms.py`, `tools/seed_import_thunks.py` | `ExportFuncs.py`, `SeedPointerTables.py` | — |
| Static recompiler | Translate x86-32 instructions, emit module bodies, native dispatch tables, DLL shims, and override routing | `tools/recomp.py`, `tools/recomp_overrides.py`, `src/recomp/` | `tools/recomp.py:main` | — |
| PE and x86 runtime | Map/relocate PE images, bind imports, dispatch guest calls by module/address, and retain recompiled super bodies | `src/native/pe_map.c`, `src/native/x86rt_native.c`, `src/recomp/x86rt.h` | `modules_init`, `x86_call`, `x86_register_override` | — |
| Runtime execution engine | Run guest code the recompiler never translated: engine selection, the substrate/x86port state bridge, the caller's-return-address contract, and the call-out back to the dispatcher | `src/native/x86_engine.c`, `shared/x86port` (consumed) | `x2_engine_init`, `x2_engine_call`, `x2_engine_call_taken` | [jit-common I004](../../../shared/jit-common/docs/issues/I004-xmen2-runtime-execution-entry-conditions.md) |
| Engine call-frame stack | The engine's per-thread interpreted-call stack: push/pop, depth (longjmp-restored), the innermost frame for the intercept checks, and the fault-dump walk. Thread-local because `guest_quantum()` lets guest threads interleave inside `x2_engine_call` (issue #140) | `src/native/x86_engine_frames.{c,h}` | `engine_frame_push`, `engine_frame_top`, `engine_frame_depth` | [issue #140](issues/0140-jit-engine-black-screen-after-legal-splash.md) |
| Engine JIT diagnostics | Apply `jit.cache` / `jit.verify` / `jit.profile` from the layered CVars to a fresh JIT engine and print what the verify cross-check covered at shutdown | `src/native/x86_engine_jit_diag.{c,h}` | `x86_engine_jit_diag_configure`, `x86_engine_jit_diag_report` | [issue #140](issues/0140-jit-engine-black-screen-after-legal-splash.md) |
| Engine JIT hot-block profile | `jit.profile=<slots>` arms x86port's execution-weighted block-entry histogram; `x2_engine_report` prints the top 40 guest EIPs with symbol names at shutdown. Diagnostic for "where does in-game guest time go". | `src/native/x86_engine.c` (report), `vendor/shared/x86port/src/x86port/jit_profile.{c,h}` | `x86p_jit_engine_set_profile`, `x86p_jit_profile_top` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Engine hand-back predicates | Decide, frame-stack-independently, when an address is host code this dispatcher owns (import thunk, return trampoline, resolved override body) -- the predicates x86port's JIT calls between blocks and at translation time, and the interpreter loop's inline equivalent | `src/native/x86_engine_intercept.{c,h}` | `x86_engine_jit_intercept`, `x86_engine_jit_boundary`, `x86_engine_host_body_at` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Engine hand-back servicing | Run the thunk / override body at a hand-back point (with a bridge-free `_ftol` fast path) and, via `jit.inline_dispatch`, let the JIT run carry on in place instead of unwinding `x86p_jit_engine_run` per crossing; owns the per-thread interpreter call-context stack | `src/native/x86_engine_dispatch.{c,h}` | `x86_engine_jit_dispatch`, `x86_engine_run_host_at`, `x86_engine_call_ctx_push` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Engine thread-manager report | Read libIGCore's private `igThreadManager` layout out of guest memory for the shutdown/heartbeat report (issue #61), every dereference bounds-checked; split from the scheduler in `threads.c` | `src/native/threads_engine_report.c` | `guest_engine_thread_report` | [issue #140](issues/0140-jit-engine-black-screen-after-legal-splash.md) |
| Engine seam proof | Run a guest program of the engine's own before the game starts, and check the call-out predicate against both answers -- so a run that never enters the engine is still evidence the bridge works | `src/native/x86_engine_selftest.c` | `x2_engine_selftest` | [jit-common I004](../../../shared/jit-common/docs/issues/I004-xmen2-runtime-execution-entry-conditions.md) |
| Engine take set | Make the substrate DECLINE named entry points, ranges, or a whole module, so real game bodies run through the engine and a divergence can be bisected; refuses host code, unmapped addresses, and a take set with no engine | `src/native/x86_engine_take.c` | `x2_take_init`, `x2_take_has`, `x2_take_validate` | [jit-common I004](../../../shared/jit-common/docs/issues/I004-xmen2-runtime-execution-entry-conditions.md) |
| Stop-path diagnostics | Everything worth reading when a run aborts, in one call: where the engine is, the peek table, the reached set, and the boundary ring | `src/native/x86_diag_dump.c` | `x86_diag_dump` | — |
| Dispatch loop | Which engine runs the body at a guest address: the take set, then the substrate, then the engine's miss path, then a named abort | `src/native/x86_dispatch.c` | `x86_dispatch`, `x86_tail_dispatch` | — |
| Dispatch failure reporting | Explain a dispatch that found no body: unbound import, owning module, who dispatched there, and the diagnostic dump | `src/native/x86_dispatch_report.c` | `x86_report_missing_body`, `x86_report_where` | — |
| Guest address space | Translate 32-bit guest addresses through the host arena, retain exact Win32 4 KiB page state, and coalesce host protection at the platform page granule | `src/native/guest_memory.c`, `src/native/guest_memory.h`, `src/native/platform_mman.h` | `guest_memory_init`, `guest_memory_pointer`, `guest_memory_map_fixed` | — |
| Guest services | Implement KERNEL32, CRT, registry, paths, COM, GDI, WinMM, sockets, heap, threads, timing, and DirectSound on the host | `src/native/kernel32.c`, `src/native/crt.c`, `src/native/advapi32.c`, `src/native/win_path.c`, `src/native/dsound.c` | import registration in each owner | — |
| Statically-linked CRT helper overrides | Native stand-ins for MSVC CRT routines XMen2.exe links into its own image (not imports), so the JIT need not interpret their x87 bodies per call -- currently `_ftol2` at `0x0067217c`, sharing `x87_crt_ftol` | `src/native/crt_static_overrides.{c,h}` | `x2_crt_ftol2`, `crt_static_overrides_register` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Default boot | Compose stored boot mode, player selection, Continue, direct-map startup, splash policy, and retail menu transition | `src/config/boot_mode.c`, `src/native/boot_mode_policy.c`, `src/native/boot_mode_runtime.c`, `src/native/boot_player_selection.c`, `src/native/continue_runtime.c`, `src/native/boot_menu_transition.c`, `src/native/startup.c` | override registration in `startup.c` | [Boot](RE/boot.md) |
| Save runtime bridges | Connect guest save/autosave/Continue calls to the pure save owners; project the eleven-entry Load Game catalog over the fixed ten-row retail dialog and route autosave through the shared exact-leaf loader | `src/save/`, `src/native/autosave_runtime.c`, `src/native/continue_runtime.c`, `src/native/exact_save_load.c`, `src/native/load_game_menu_runtime.c`, `src/native/save_trace_runtime.c` | override registration in the native bridge files | [Boot](RE/boot.md) |
| DirectInput host | Enumerate physical and virtual pads, maintain COM device state, translate controller slots, and publish binding sets | `src/native/dinput*.c`, `src/input/`, `src/config/input_assignments.c` | `dinput_system.c`, `player_input.c` | [Co-op participation](RE/co_op_participation.md) |
| Controller prompt composition | Map retail control names to port-private prompt codepoints and compose popup/label prose | `src/native/pad_glyphs.c`, `src/native/prompt_labels.c`, `src/native/dialog_prompts.c`, `src/native/prompt_glyphs.c` | `x2_prompt_label`, `x2_prompt_glyphs_enabled` | [Xbox button prompts](features/xbox-button-prompts.md) |
| Prompt font integration | Publish font-owned baseline and layout metrics for private prompt codepoints at the engine font boundary | `src/native/prompt_glyph_metrics.c`, `src/native/ui_text_scale.c` | `x2_prompt_glyph_publish_metrics` | [Text and prompts](RE/text.md) |
| Prompt quad capture | Preflight whole Alchemy text strings, preserve engine coordinates/color, queue native quads, and retain collapsed retail emitter calls for batch semantics | `src/native/prompt_glyph_draw.c`, `src/native/prompt_glyph_quads.c` | override registration in `prompt_glyph_draw.c`; `x2_prompt_quads_add` | [Text and prompts](RE/text.md) |
| Prompt batch and transform | Bracket Alchemy non-indexed text batches, snapshot the engine-finalized UI transform, and submit queued prompt quads before stock label pixels | `src/native/prompt_glyph_batch.c`, `src/native/ui_transform.c` | `x2_prompt_glyph_batch_draw_nonindexed`, `x2_prompt_glyph_batch_update_context_state` | [Text and prompts](RE/text.md) |
| Prompt atlas | Rasterize redistributable SVG sources into a generated RGBA atlas and own its compiled storage | `tools/render_prompt_glyphs.py`, `src/native/prompt_glyph_atlas.c`, `src/recomp/gen/` | `tools/render_prompt_glyphs.py:main` | [Text and prompts](RE/text.md) |
| Prompt GPU pass | Retain atlas/vertex resources and render engine-plane prompt quads with the batch MVP and alpha blending | `src/gpu/gpu_prompt_glyphs.c`, `src/gpu/gpu_matrix.c` | `gpu_prompt_glyphs_render` | [Text and prompts](RE/text.md) |
| D3D8-to-GPU translation | Convert D3D8 resources, state blocks, transforms, fixed-function state, and VS 1.1 draws into shared GPU draw descriptions | `src/d3d8/`, `src/gpu/gpu_draw.c` | `d3d8_drawcall.c`, `gpu_draw_submit` | — |
| Lighting and shadows | Translate title lights, classify caster/receiver packets, own shadow resources/pass, and expose the Video setting | `src/d3d8/d3d8_lightlog.c`, `src/gpu/shadow_policy.c`, `src/gpu/gpu_shadow.c`, `src/config/settings.c`, `src/ui/settings_document.cpp` | `gpu_shadow_render` | [Shadows](RE/shadows.md) |
| Presentation and capture | Own boot and post-retail-default resolution publication, transactional logical colour/depth resizing, D3D8 presentation-state publication, aspect-fit composition, swapchain lifetime, retained-frame readback, and control-channel screenshots | `src/presentation/display_mode_seed.c`, `src/native/display_mode_runtime.c`, `src/presentation/live_resolution.c`, `src/d3d8/d3d8_live_resolution.c`, `src/gpu/gpu_present.c`, `src/gpu/gpu_capture.c`, `src/native/win32_sdl.c`, `src/native/control_screenshot.c` | `x2_override_display_settings_load`, `x2_live_resolution_apply`, `gpu_present_composite`, `control_screenshot_request` | — |
| Win32 window events and mouse | Translate SDL window/mouse/touch events into the ordered Win32 queue consumed by the retained Alchemy WndProc, map through logical-output aspect fit, and arbitrate the one visible cursor | `src/native/win32_events.c`, `src/native/win32_pointer.{c,h}`, `src/native/win32_mouse.c`, `src/native/win32_sdl.c` | `imp_USER32_PeekMessageA`, `imp_USER32_DispatchMessageA`, `x2_win32_pointer_translate_touch` | — |
| Live control and recording | Publish live-session discovery, accept loopback control requests, merge scripted input on guest polls, route bounded status/save/frame-timing responses, and record delivered input | `src/native/control.c`, `src/native/control_command_bridge.h`, `src/native/control_status.c`, `src/native/control_status_route.c`, `src/native/control_save_route.c`, `src/native/control_performance_route.c`, `src/native/control_query.c`, `src/native/live_session.c`, `src/input/input_record.c`, `tools/x2ctl.py` | `control_start`, `tools/x2ctl.py:main` | — |
| In-game cutscene player | Complete a control-lock epoch through causally owned timed events and BehavEd fibers; use deterministic conversations only as subordinate payloads; suppress their exact response/line presenters while synchronous skip is active | `src/native/cutscene_player.c`, `src/native/cutscene_dialogue.c`, `src/native/cutscene_event_player.c`, `src/native/behaved_player.c`, `src/native/conversation_player.c`, `src/native/cutscene_skip_publication.c` | override registration in each player; action-20 edge in `cutscene_player.c` | [Cutscene player](RE/cutscene_player.md) |
| Native movie playback | Bridge libMovie/CRI guest objects to the media decoder and streaming movie voice | `src/native/movie.c`, `src/native/movie_image_layout.c`, `src/media/`, `src/audio/`, `src/d3d8/d3d8_texture_luma.c` | override registration in `movie.c` | [FMV](RE/fmv.md) |
| Port settings overlay | Compose persistent settings, controller/touch policy, assignment rows, and pause-menu command integration | `src/ui/`, `src/config/`, `src/native/options_menu.c`, `tools/make_port_pause_menu.py`, `tools/prepare_native_assets.py`, `assets/ui/settings.rcss` | `rmlui_ui_init`, override registration in `options_menu.c` | [Prior art](prior-art.md) |
| AppImage setup and release | Select and validate the complete read-only PC install through the first-run SDL3 prompt, transactionally prepare nested ZIP installs without damaging a prior valid extraction, resolve portable UI resources, and stage a game-file-free Linux desktop package | `src/native/install_picker.{cpp,h}`, `src/native/install_archive.{cpp,h}`, `src/native/install_validation.{cpp,h}`, generated `install_requirements.h`, `src/config/config_directory.{c,h}`, `src/ui/ui_resources.{cpp,h}`, `packaging/`, `tools/package_appimage.py`, shared `lucent::zip` | `x2_install_picker_choose`, `x2_install_validate_executable`, `x2_install_archive_prepare`, `x2_ui_resource_path`, `tools/package_appimage.py:main` | — |
| Android setup and release | Own the no-terminal setup Activity, SAF permissions and app-private staging; consume the pinned `shared/android-port` prefix; compose title validation, signed APK, launcher icon, title-authored touch feedback, touch-only relocation of the retained retail HUD, and named-device performance evidence collection | `android/`, `src/native/android_bridge.{cpp,h}`, `src/native/install_validation.{cpp,h}`, `src/input/touch_controls.{cpp,h}`, `src/input/touch_runtime.{cpp,h}`, `src/presentation/touch_layout.{c,h}`, `src/input/gameplay_control.{c,h}`, `src/native/touch_hud_runtime.{c,h}`, `src/ui/touch_document.{cpp,h}`, `assets/ui/touch_controls.rcss`, `tools/build_android.py`, `tools/android_qualify.py`, CMake Android branch | `XMen2SetupActivity`, `XMen2GameActivity`, `x2_install_validate_executable`, `x2_touch_runtime_event`, overrides at `FUN_005a43d0`/`FUN_005a3320`, `touch_document_update`, `tools/android_qualify.py:main`, `tools/build_android.py:main` | [Android release](android-release.md) |
| Stock-oracle probes | Instrument stock D3D8/title calls, cache driven controls, sample live guest state, and compare traces/frames | `tools/proxy_d3d8/`, `tools/oracle.py`, `tools/oracle_probe.py`, `tools/oracle_compare.py`, `tools/oraclediff.py`, `tools/shot_compare.py` | each tool's `main` | — |
| Shared Alchemy formats | Own IGB, animation, XMLB, ARK, controller abstraction, and reusable viewers outside the title port | separate `alchemy` repository, resolved by `tools/alchemy_path.py` | Alchemy library/viewer entry points | external Alchemy docs |
| Xbox recompilation | Own XBE lift/runtime integration and consume the maintained external recompilation fork | `xbox/`, `xbox/xboxrecomp.lock`, `vendor/xboxrecomp/`, `tools/xbox_relift.py`, `tools/xbox_discover.py`, `tools/get_xboxrecomp.py`, `tools/xbe_query.py` | `xbox/src/main.c` | — |
| Structural enforcement | Enforce host-source ownership limits and keep Python automation linted | `tools/check_structure.py`, `tools/lint.py` | ctest `structure`, `python_lint` | — |

## Source tree

Generated module bodies under `src/recomp/` dominate the line counts and are
regenerated rather than hand-edited. This tree was produced with
`codemap.py tree src --depth 1 --min-lines 1`.

```text
src/  —  9,125,093 lines, 406 files
├─ app/  312 lines  3 files  [.c .h]
├─ audio/  222 lines  2 files  [.c .h]
├─ config/  966 lines  14 files  [.c .h .cpp]
├─ d3d8/  11,077 lines  32 files  [.h .c]
├─ gpu/  6,172 lines  31 files  [.c .h]
├─ input/  1,054 lines  18 files  [.c .h]
├─ media/  1,090 lines  13 files  [.h .c]
├─ native/  31,579 lines  154 files  [.c .h]
├─ oracle/  294 lines  1 files  [.h]
├─ presentation/  397 lines  8 files  [.c .h]
├─ recomp/  9,067,151 lines  97 files  [.c .h]
├─ save/  996 lines  15 files  [.h .c]
├─ ui/  888 lines  8 files  [.cpp .hpp .h .c]
├─ vulkan/  2,187 lines  8 files  [.c .h]

TOTAL: 9,125,093 lines across 406 files in 1 root(s)
```

## Where does X go?

- **A new native override** → the smallest title/engine subsystem owner in
  `src/native/`, with `x86_register_override` beside its implementation; routing
  support stays in `tools/recomp_overrides.py` and `src/native/x86rt_native.c`.
- **A new Win32 import** → the matching DLL owner in `src/native/`; reusable
  host policy should be extracted into its cohesive subsystem directory.
- **A D3D8 COM/state/resource rule** → `src/d3d8/`; generic draw/resource
  lifetime belongs in `src/gpu/`.
- **A prompt label or control-name mapping** → `src/native/prompt_labels.c` or
  `src/native/pad_glyphs.c`; prompt layout belongs in
  `src/native/prompt_glyph_metrics.c`; engine glyph capture belongs in
  `src/native/prompt_glyph_draw.c`; batch timing/transform belongs in
  `src/native/prompt_glyph_batch.c` and `src/native/ui_transform.c`; pixels
  belong in `src/gpu/gpu_prompt_glyphs.c`.
- **A reusable matrix operation** → `src/gpu/gpu_matrix.c`; engine matrix
  capture remains in its native bridge and D3D8 matrix lowering remains in
  `src/d3d8/`.
- **A setting or stored assignment** → `src/config/`; window-mode and live
  logical-resolution application belong in `src/presentation/`; D3D8/GPU
  target mutation stays behind their presentation seams; the binary-grounded
  guest settings-load bridge belongs in `src/native/display_mode_runtime.c`;
  UI editing belongs in `src/ui/`.
- **A host mouse/window event** → SDL lifecycle and guest message dispatch
  belong in `src/native/win32_events.c`; pointer geometry plus physical/touch
  message publication belong in `src/native/win32_pointer.c`; pure queue and
  cursor policy belong in `src/native/win32_mouse.c`.
- **A player/controller rule** → `src/input/`; DirectInput COM publication and
  guest slot translation belong in `src/native/dinput*.c`.
- **A save format/path/catalog policy** → `src/save/`; guest call bridges belong
  in `src/native/` files named for Continue, autosave, or tracing.
- **A movie decode/timing rule** → `src/media/`; movie voice mixing belongs in
  `src/audio/`; guest ABI translation belongs in `src/native/movie.c`.
- **A reusable Alchemy asset reader or engine abstraction** → the separate
  `alchemy` repository; only title-specific composition belongs here.
- **A binary discovery or translation rule** → `tools/` or
  `tools/ghidra_scripts/`; generated code remains in `src/recomp/`.
- **An Xbox lifter/runtime change** → the maintained
  `SomeoneIsWorking/xboxrecomp` fork; title integration belongs in `xbox/src/`.
