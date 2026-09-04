# Codemap

The single-page answer to **which subsystem owns each responsibility, where it
lives, and where related work should go**. Capability state belongs in
`docs/project-state.md`; epic intent belongs in `docs/project-goals.md`; atomic
work belongs in `docs/issues/`; proof and tool trust belong in `docs/info/`;
ordered reverse-engineering dependencies belong in `docs/re-frontier.md`.

## Architecture

```text
          authenticated user PE images
                      |
                      v
       runtime PE mapping + import binding
                      |
                      v
        shared/x86port x86-64 runtime JIT
                      |
          +-----------+-----------+
          |                       |
          v                       v
  original guest body      subsystem native override
          |                       |
          +-----------+-----------+
                      |
          title-owned runtime dispatcher
                      |
          +-----------+-----------+
          |                       |
          v                       v
 host Win32 services       title/engine bridges
          |                       |
          +-----------+-----------+
                      |
              host D3D8 translation
                      |
                      v
         shared SDL_GPU renderer/presenter
                      |
                      v
          SDL window/input/audio boundary
```

Configuration, input, saves, presentation, media/audio, and RmlUi are cohesive
host owners composed at the native boundary. `shared/alchemy` owns partial
neutral format/render-data and input foundations plus XMLB/ARK tooling.
`x2native` now composes its SDL-free input target beside x86port through the
title-local controller adapter below; retained Alchemy behavior or the other
title-local subsystem rows still own every remaining product path.
Explicit interpreter mode is owned by a separately built x86port test target,
outside the gameplay configuration surfaces. A bounded, counted internal
fallback belongs to the shared x86port JIT and is not a selectable engine.

## Source ownership

The directory rows establish the broad ownership boundary. The detailed rows
below name cross-directory seams and the entry points used to extend them.

| Subsystem | Responsibility | Location | Entry point | Deep doc |
|---|---|---|---|---|
| Movie audio | Streaming movie voice and DirectSound-facing audio sink | `src/audio/` | `movie_audio.c` | [FMV](RE/fmv.md) |
| Gameplay cutscene player | Own BehavEd command/scheduler and timed-event steps through authored control release; silence exact dialogue and script-audio presenters only during synchronous completion | `src/native/cutscene_player.c`, `src/native/behaved_context.c`, `src/native/behaved_player.c`, `src/native/cutscene_event_player.c`, `src/native/cutscene_dialogue.c`, `src/native/cutscene_script_audio.c` | `x2_cutscene_player_finish`, `behaved_context_run` | [Cutscene player](RE/cutscene_player.md) |
| Persistent configuration | Settings schema, OS user configuration directory, storage, boot mode, controller assignments, and the single compatibility boundary for legacy process-environment reads | `src/config/` | `config_directory.c`, `environment.c`, `settings.c`, `settings_store.c` | [Co-op participation](RE/co_op_participation.md) |
| Runtime CVars | Register optional title/JIT diagnostics and tuning through Lucent's layers; execution architecture is invariant and is never a CVar, environment, or CLI selector | `src/config/runtime_cvars.{cpp,h}` | `x2_runtime_config_init` | [Strategy](strategy.md) |
| Runtime logging | Provide the one configurable project logger and route subsystem diagnostics through one call per site; no subsystem-owned stderr gates | shared Lucent logger plus `src/native/x2_log.{c,h}` title composition | `x2_log_error` | — |
| Direct3D 8 host | COM ABI, resources/formats, state, two-stage texture lowering, fixed/programmable vertex processing, and renderer diagnostics | `src/d3d8/`, `src/gpu/gpu_texture_format.c` | `d3d8_d3d8.c`, `d3d8_device.c`, `d3d8_drawcall.c`, `d3d8_texture_stage.c` | [Shadows](RE/shadows.md) |
| D3D8 texture provenance | Own immutable runtime texture metadata plus successful 2D base-level content fingerprint/revision, without asset-name claims | `src/d3d8/d3d8_texture_provenance.{c,h}` | initialized and updated by `d3d8_resource.c`, snapshotted into `D3D8DrawRequest` | — |
| D3D8 selector evidence | Passively correlate an exact texture or untextured draw class with pre-build geometry, accepted/refused lowering results, and world-matrix ancestry, without asserting identity the evidence does not establish | `src/d3d8/d3d8_selector_probe.{c,h}`, `src/d3d8/d3d8_selector_probe_json.{c,h}`, `src/d3d8/d3d8_selector_probe_bridge.c`, `tools/selector_probe.py` | `d3d8_selector_probe_build_request`, `tools/selector_probe.py` | — |
| Host GPU | SDL_GPU device, deterministic frame initialization, uploads, draws, capture, composition, frame submission, bounded exact frame timing, prompt glyphs, and shadow pass | `src/gpu/` | `gpu_device.c`, `gpu_draw.c`, `gpu_frame_timing.{c,h}`, `gpu_frame_timing_report.{c,h}` | [Text and prompts](RE/text.md), [Shadows](RE/shadows.md) |
| Input policy | Binding rows, device identity/admission, player association, participation, probing lifecycle, exact recording, and authored virtual touch action mapping | `src/input/` | `player_input.c`, `input_record.c`, `touch_controls.cpp` | [Co-op participation](RE/co_op_participation.md), [Android release](android-release.md) |
| Media decode | SFD demux, video decode, ADX decode, timing, drain, and probe policy | `src/media/` | `fmv_player.c` | [FMV](RE/fmv.md) |
| Native host and bridges | PE/runtime composition, POSIX Win32 services, title and engine overrides, live control, boot, input publication, and diagnostics; `engine_file_path.c` bridges the retail registry-backed file search to virtual `C:\`; fatal signals report a symbolizable host PC before the guest-state ring | `src/native/` | `x2native.c`, `engine_file_path.c`, `fault_report.c`, `x86rt_native.c` | [Boot](RE/boot.md) |
| Generated native assets | Hold build-generated redistributable prompt-atlas and font-tier headers; never guest instructions or translated bodies | `src/gen/` | `prompt_glyph_atlas.h`, `font_tier_ratio.h` | [Text and prompts](RE/text.md) |
| Presentation policy | Window settings, transactional live logical-resolution changes including retained title display state, aspect fit, display-mode publication, and boot blackout | `src/presentation/`, `src/native/display_mode_runtime.{c,h}` | `x2_live_resolution_apply`, `x2_display_mode_runtime_apply`, `x2_override_display_settings_load` | — |
| Retail dialog selected-row scale | Own the shared 800x600 retail UI reference and extend the title's evidenced selected-row transform formula above that reference at its exact call site | `src/presentation/retail_ui_design.h`, `src/native/dialog_selection_scale_policy.{c,h}`, `src/native/dialog_selection_scale.{c,h}` | `x2_dialog_selection_scale`, `x2_dialog_selection_transform` | [Issue #133](issues/0133-retail-dialog-selection-highlight-is-missing.md) |
| Guest ABI support | Hand-written CPU/ABI compatibility helpers shared by title-native call boundaries | `src/runtime/x86_abi/` | narrow helper named for the ABI contract | — |
| Save model | Retail save directory, catalog, Continue policy, autosave policy/format/storage, Load Game logical-window policy, and trace model | `src/save/` | `save_directory.c`, `save_catalog.c`, `load_game_menu_policy.c` | [Boot](RE/boot.md) |
| Port settings UI | RmlUi lifetime, settings document, controller rows, and overlay state | `src/ui/` | `rmlui_ui.cpp`, `settings_document.cpp` | [External provenance](prior-art.md) |
| ARK visual-context backend | Alchemy visual-context class construction and slot bridges | `src/vulkan/` | `igvk_context.c`, `igvk_ark.c` | — |

## Cross-directory subsystem seams

| Subsystem | Responsibility | Location | Entry point | Deep doc |
|---|---|---|---|---|
| Bootstrap and launcher | Validate user images, provision pinned redistributable dependencies/native assets, configure, build, and launch the JIT gameplay product without emitting guest code | `bootstrap.py`, `tools/provision.py`, `tools/run.py`, `run.sh` | `bootstrap.py:main`, `tools/run.py:main` | [Strategy](strategy.md) |
| Binary analysis evidence | Recover addresses, call contracts, and data semantics needed by native overrides; never emit product guest code or an ahead-of-time execution map | `tools/`, `docs/RE/`, `docs/info/` | the smallest analysis tool and its living evidence document | — |
| PE runtime | Discover, authenticate, map, and relocate user PE images at runtime; bind imports and resolve module-qualified native overrides/original calls | `src/native/guest_modules.c`, `src/native/pe_map.c`, `src/native/x86rt_native.c`, `src/native/guest_body.c` | `modules_init`, `x86_register_override`, `x86_guest_body` | — |
| Runtime execution engine | Compose the product-only x86port JIT, caller return contract, bounded exits, and call-out to the title dispatcher; CPU decode/semantics/emission remain in canonical `shared/x86port` | `src/native/x86_engine*.{c,h}`, consumed `shared/x86port` | `x2_engine_init`, `x2_engine_call` | [Strategy](strategy.md) |
| Diagnostic interpreter | Provide an independent CPU oracle only in a separately built x86port test/diagnostic target, absent from gameplay selectors | canonical `shared/x86port` tests (target) | focused differential test executable | [Strategy](strategy.md) |
| Guest-call stack | The JIT boundary's one intrusive per-thread stack of live host-owned guest calls: native-import dispatch context, longjmp restoration, return interception, and fault-dump walk all share these nodes | `src/native/x86_guest_call_stack.{c,h}` | `x86_guest_call_push`, `x86_guest_call_top`, `x86_guest_call_restore` | [issue #140](issues/0140-jit-engine-black-screen-after-legal-splash.md) |
| Engine JIT diagnostics | Apply cache/profile controls to a fresh product JIT and report translation coverage plus every bounded fallback entry/interval; explicit interpreter differential verification belongs in the separate test target | `src/native/x86_engine_jit_diag.{c,h}`, canonical `shared/x86port` tests (target) | `x86_engine_jit_diag_configure`, `x86_engine_jit_diag_report` | [issue #140](issues/0140-jit-engine-black-screen-after-legal-splash.md) |
| Engine JIT hot-block profile | `jit.profile=<slots>` arms x86port's execution-weighted block-entry histogram; `x2_engine_report` prints the top 40 guest EIPs with symbol names at shutdown. Diagnostic for "where does in-game guest time go". | `src/native/x86_engine.c` (report), `vendor/shared/x86port/src/x86port/jit_profile.{c,h}` | `x86p_jit_engine_set_profile`, `x86p_jit_profile_top` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Engine hand-back predicates | Decide, frame-stack-independently, when an address is host code this dispatcher owns (import thunk, return trampoline, resolved override body); x86port's JIT calls the predicates between blocks and at translation time | `src/native/x86_engine_intercept.{c,h}` | `x86_engine_jit_intercept`, `x86_engine_jit_boundary`, `x86_engine_host_body_at` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Engine hand-back servicing | Run the thunk/override body at a hand-back point against the current canonical CPU and let the JIT continue in place when the boundary permits | `src/native/x86_engine_dispatch.{c,h}` | `x86_engine_jit_dispatch`, `x86_engine_run_host_at` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Native-import fast path | Fast-path pure leaf import thunks directly on canonical x86port CPU state; retain a diagnostic A/B against ordinary native-import dispatch | `src/native/x86_import_fastpath.{c,h}` | `x86_import_fastpath_dispatch`, `x86_import_fastpath_init` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Engine thread-manager report | Read libIGCore's private `igThreadManager` layout out of guest memory for the shutdown/heartbeat report (issue #61), every dereference bounds-checked; split from the scheduler in `threads.c` | `src/native/threads_engine_report.c` | `guest_engine_thread_report` | [issue #140](issues/0140-jit-engine-black-screen-after-legal-splash.md) |
| Engine seam proof | Run a guest program before the game starts and check the call-out predicate against both answers, proving the shipping x86port path and canonical CPU state are live | `src/native/x86_engine_selftest.c` | `x2_engine_selftest` | [Strategy](strategy.md) |
| Stop-path diagnostics | Everything worth reading when a run aborts, in one call: where the engine is, the peek table, and the boundary ring | `src/native/x86_diag_dump.c` | `x86_diag_dump` | — |
| Dispatch loop | Route host imports/native overrides first and every remaining guest address to the product JIT; a miss is a named abort | `src/native/x86_dispatch.c` | `x86_dispatch` | — |
| Dispatch failure reporting | Explain a dispatch that found no body: unbound import, owning module, who dispatched there, and the diagnostic dump | `src/native/x86_dispatch_report.c` | `x86_report_missing_body`, `x86_report_where` | — |
| Guest address space | Translate 32-bit guest addresses through the host arena, retain exact Win32 4 KiB page state, and coalesce host protection at the platform page granule | `src/native/guest_memory.c`, `src/native/guest_memory.h`, `src/native/platform_mman.h` | `guest_memory_init`, `guest_memory_pointer`, `guest_memory_map_fixed` | — |
| Guest services | Implement KERNEL32, CRT, registry, paths, COM, GDI, WinMM, sockets, heap, threads, timing, and DirectSound on the host | `src/native/kernel32.c`, `src/native/crt.c`, `src/native/advapi32.c`, `src/native/win_path.c`, `src/native/dsound.c` | import registration in each owner | — |
| In-image CRT helper overrides | Native stand-ins for MSVC CRT routines linked inside XMen2.exe (not imports), so the JIT avoids their repeated guest bodies -- currently `_ftol2` at `0x0067217c`, sharing `x87_crt_ftol` | `src/native/crt_in_image_overrides.{c,h}` | `x2_crt_ftol2`, `crt_in_image_overrides_register` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Native IMA ADPCM decode | Native overrides for XMen2.exe's statically-linked IMA ADPCM decoders (`0x00616770` mono, `0x00616880` stereo), replacing a ~7%-of-guest-time per-nibble interpreted loop; `audio.adpcm_verify` re-runs the guest body and aborts on any mismatch | `src/native/audio_adpcm.{c,h}`, `src/native/audio_adpcm_verify.c` | `ima_adpcm_step`, `x2_override_00616770`, `x2_override_00616880`, `audio_adpcm_verify_or_abort` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Native vertex colour swap | Native override for libIGGfx.dll's `igDxVertexArray1_1::` range colour-channel swap (`0x10046ce0`), replacing a ~4.4%-of-guest-time per-vertex BGRA<->RGBA loop; `gfx.vtx_swizzle_verify` re-runs the guest body and aborts on any mismatch | `src/native/vertex_color_swizzle.{c,h}`, `src/native/vertex_color_swizzle_verify.c` | `vtx_color_swizzle_word`, `x2_override_10046ce0`, `vtx_swizzle_verify_begin`/`_end` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Native immediate vertex builder | Native override for XMen2.exe's `CDxImmediateBuilder::addVertex` (`0x005840a0`), replacing ~15% of in-game JIT block dispatches across immediate geometry, text, HUD, and decals; `gfx.vtx_builder_verify` re-runs the guest body and aborts on any mismatch | `src/native/vertex_builder.{c,h}`, `src/native/vertex_builder_verify.c` | `x2_override_005840a0`, `vtx_builder_verify_begin`/`_end` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Native scene graph attribute stack reset | Native overrides for libIGSg.dll's `igAttrStackManager::reset` (`0x10034d30`) and `igAttrStack::customReset` (`0x10034d10`), replacing ~3.26M inner-loop calls per 2000 in-game frames; `sg.attr_stack_verify` re-runs the guest body and aborts on any mismatch | `src/native/attr_stack.{c,h}`, `src/native/attr_stack_verify.{c,h}` | `x2_override_10034d30`, `x2_override_10034d10`, `attr_stack_custom_reset`, `attr_stack_verify_begin`/`_end` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Native audio channel poll | Native override for XMen2.exe's per-frame audio channel completion sweep (`0x00594500`), replacing a 24-entry JIT loop (~1.2% of guest block-entry weight) and its per-channel `IDirectSoundBuffer::GetStatus` crossings; `audio.channel_poll_verify` re-runs the guest body and aborts on any mismatch | `src/native/audio_channel_poll.{c,h}`, `src/native/audio_channel_poll_verify.{c,h}` | `x2_override_00594500`, `audio_channel_poll_run`, `audio_channel_poll_verify`; `dsound_buffer_is_playing` / `dsound_buffer_release_guest` in `src/native/dsound.c` | [issue #141](issues/0141-engine-jit-menu-in-game-throughput-is-capped-35.md) |
| Default boot | Compose stored boot mode, player selection, Continue, direct-map startup, splash policy, and retail menu transition | `src/config/boot_mode.c`, `src/native/boot_mode_policy.c`, `src/native/boot_mode_runtime.c`, `src/native/boot_player_selection.c`, `src/native/continue_runtime.c`, `src/native/boot_menu_transition.c`, `src/native/startup.c` | override registration in `startup.c` | [Boot](RE/boot.md) |
| Save runtime bridges | Connect guest save/autosave/Continue calls to the pure save owners; project the eleven-entry Load Game catalog over the fixed ten-row retail dialog and route autosave through the shared exact-leaf loader | `src/save/`, `src/native/autosave_runtime.c`, `src/native/continue_runtime.c`, `src/native/exact_save_load.c`, `src/native/load_game_menu_runtime.c`, `src/native/save_trace_runtime.c` | override registration in the native bridge files | [Boot](RE/boot.md) |
| DirectInput host | Enumerate physical and virtual pads, maintain COM device state, latch one controller sample, translate controller slots, and publish binding sets | `src/native/dinput*.c`, `src/input/directinput_controller_sample.{c,h}`, `src/config/input_assignments.c` | `dinput_system.c`, `dinput_joystick_state` | [Co-op participation](RE/co_op_participation.md) |
| Shared Alchemy controller adapter | Publish the latched PC sample to neutral `alchemy::input`, preserve stable device lifecycle, expose the recovered guest callback seam, and A/B compare the shared state with retained DirectInput through typed configuration and Lucent diagnostics | `src/input/ig_controller_manager_adapter.{cpp,hpp}`, `src/input/alchemy_controller_bridge.{cpp,h}` | `x2_alchemy_controller_observe`, `IgControllerManagerAdapter` | [Shared input boundary](../../../shared/alchemy/docs/input.md) |
| Controller prompt composition | Map retail control names to port-private prompt codepoints and compose popup/label prose | `src/native/pad_glyphs.c`, `src/native/prompt_labels.c`, `src/native/dialog_prompts.c`, `src/native/prompt_glyphs.c` | `x2_prompt_label`, `x2_prompt_glyphs_enabled` | [Xbox button prompts](features/xbox-button-prompts.md) |
| Prompt font integration | Publish font-owned baseline and layout metrics for private prompt codepoints at the engine font boundary | `src/native/prompt_glyph_metrics.c`, `src/native/ui_text_scale.c` | `x2_prompt_glyph_publish_metrics` | [Text and prompts](RE/text.md) |
| Prompt quad capture | Preflight whole Alchemy text strings, preserve engine coordinates/color, queue native quads, and retain collapsed retail emitter calls for batch semantics | `src/native/prompt_glyph_draw.c`, `src/native/prompt_glyph_quads.c` | override registration in `prompt_glyph_draw.c`; `x2_prompt_quads_add` | [Text and prompts](RE/text.md) |
| Prompt batch and transform | Bracket Alchemy non-indexed text batches, snapshot the engine-finalized UI transform, and submit queued prompt quads before stock label pixels | `src/native/prompt_glyph_batch.c`, `src/native/ui_transform.c` | `x2_prompt_glyph_batch_draw_nonindexed`, `x2_prompt_glyph_batch_update_context_state` | [Text and prompts](RE/text.md) |
| Prompt atlas | Rasterize redistributable SVG sources into a generated RGBA atlas and own its compiled storage | `tools/render_prompt_glyphs.py`, `src/native/prompt_glyph_atlas.c`, `src/gen/` | `tools/render_prompt_glyphs.py:main` | [Text and prompts](RE/text.md) |
| Prompt GPU pass | Retain atlas/vertex resources and render engine-plane prompt quads with the batch MVP and alpha blending | `src/gpu/gpu_prompt_glyphs.c`, `src/gpu/gpu_matrix.c` | `gpu_prompt_glyphs_render` | [Text and prompts](RE/text.md) |
| D3D8-to-GPU translation | Convert D3D8 resources, state blocks, transforms, fixed-function state, and VS 1.1 draws into shared GPU draw descriptions | `src/d3d8/`, `src/gpu/gpu_draw.c` | `d3d8_drawcall.c`, `gpu_draw_submit` | — |
| Lighting and shadows | Translate title lights, classify caster/receiver packets, own shadow resources/pass, and expose the Video setting | `src/d3d8/d3d8_lightlog.c`, `src/gpu/shadow_policy.c`, `src/gpu/gpu_shadow.c`, `src/config/settings.c`, `src/ui/settings_document.cpp` | `gpu_shadow_render` | [Shadows](RE/shadows.md) |
| Presentation and capture | Own boot and post-retail-default resolution publication, transactional logical colour/depth resizing, D3D8 presentation-state publication, aspect-fit composition, swapchain lifetime, retained-frame readback, and control-channel screenshots | `src/presentation/display_mode_seed.c`, `src/native/display_mode_runtime.c`, `src/presentation/live_resolution.c`, `src/d3d8/d3d8_live_resolution.c`, `src/gpu/gpu_present.c`, `src/gpu/gpu_capture.c`, `src/native/win32_sdl.c`, `src/native/control_screenshot.c` | `x2_override_display_settings_load`, `x2_live_resolution_apply`, `gpu_present_composite`, `control_screenshot_request` | — |
| Win32 window events and mouse | Translate SDL window/mouse/touch events into the ordered Win32 queue consumed by the retained Alchemy WndProc, map through logical-output aspect fit, and arbitrate the one visible cursor | `src/native/win32_events.c`, `src/native/win32_pointer.{c,h}`, `src/native/win32_mouse.c`, `src/native/win32_sdl.c` | `imp_USER32_PeekMessageA`, `imp_USER32_DispatchMessageA`, `x2_win32_pointer_translate_touch` | — |
| Live control and recording | Publish live-session discovery, accept loopback control requests, merge scripted input on guest polls, route bounded status/save/frame-timing responses, and record delivered input | `src/native/control.c`, `src/native/control_command_bridge.h`, `src/native/control_status.c`, `src/native/control_status_route.c`, `src/native/control_save_route.c`, `src/native/control_performance_route.c`, `src/native/control_query.c`, `src/native/live_session.c`, `src/input/input_record.c`, `tools/x2ctl.py` | `control_start`, `tools/x2ctl.py:main` | — |
| In-game cutscene player | Complete a control-lock epoch through causally owned timed events and BehavEd fibers; use deterministic conversations only as subordinate payloads; suppress their exact response/line presenters while synchronous skip is active | `src/native/cutscene_player.c`, `src/native/cutscene_dialogue.c`, `src/native/cutscene_event_player.c`, `src/native/behaved_player.c`, `src/native/conversation_player.c`, `src/native/cutscene_skip_publication.c` | override registration in each player; action-20 edge in `cutscene_player.c` | [Cutscene player](RE/cutscene_player.md) |
| Native movie playback | Bridge libMovie/CRI guest objects to the media decoder and streaming movie voice | `src/native/movie.c`, `src/native/movie_image_layout.c`, `src/media/`, `src/audio/`, `src/d3d8/d3d8_texture_luma.c` | override registration in `movie.c` | [FMV](RE/fmv.md) |
| Port settings overlay | Compose persistent settings, controller/touch policy, assignment rows, and pause-menu command integration | `src/ui/`, `src/config/`, `src/native/options_menu.c`, `tools/make_port_pause_menu.py`, `tools/prepare_native_assets.py`, `assets/ui/settings.rcss` | `rmlui_ui_init`, override registration in `options_menu.c` | [External provenance](prior-art.md) |
| AppImage setup and release | Select and validate the complete read-only PC install through the first-run SDL3 prompt, transactionally prepare nested ZIP installs without damaging a prior valid extraction, resolve portable UI resources, and stage a game-file-free Linux desktop package | `src/native/install_picker.{cpp,h}`, `src/native/install_archive.{cpp,h}`, `src/native/install_validation.{cpp,h}`, generated `install_requirements.h`, `src/config/config_directory.{c,h}`, `src/ui/ui_resources.{cpp,h}`, `packaging/`, `tools/package_appimage.py`, shared `lucent::zip` | `x2_install_picker_choose`, `x2_install_validate_executable`, `x2_install_archive_prepare`, `x2_ui_resource_path`, `tools/package_appimage.py:main` | — |
| Android setup and release | Own the no-terminal setup Activity, SAF permissions and app-private staging; consume the pinned `shared/android-port` prefix; compose title validation, signed APK, launcher icon, title-authored touch feedback, touch-only relocation of the retained retail HUD, and named-device performance evidence collection | `android/`, `src/native/android_bridge.{cpp,h}`, `src/native/install_validation.{cpp,h}`, `src/input/touch_controls.{cpp,h}`, `src/input/touch_runtime.{cpp,h}`, `src/presentation/touch_layout.{c,h}`, `src/input/gameplay_control.{c,h}`, `src/native/touch_hud_runtime.{c,h}`, `src/ui/touch_document.{cpp,h}`, `assets/ui/touch_controls.rcss`, `tools/build_android.py`, `tools/android_qualify.py`, CMake Android branch | `XMen2SetupActivity`, `XMen2GameActivity`, `x2_install_validate_executable`, `x2_touch_runtime_event`, overrides at `FUN_005a43d0`/`FUN_005a3320`, `touch_document_update`, `tools/android_qualify.py:main`, `tools/build_android.py:main` | [Android release](android-release.md) |
| Stock-oracle tooling | Build Python-driven libIG proxy/trace shims, instrument the stock D3D8 boundary, cache driven controls, and compare retained frames | `tools/build_shim.py`, `tools/build_stocklog.py`, `tools/proxy_d3d8/`, `tools/oracle.py`, `tools/shot_compare.py` | each tool's `main` | — |
| Shared Alchemy foundations and engine boundary | Own neutral IGB/image/mesh/raster/Enbaya and input libraries plus XMLB/ARK tools; remain independent of x86port/x360port and accept product responsibility only through separately linked adapters and conformance evidence | external `shared/alchemy`; X-Men 2 links `alchemy::input` beside `x86port_runtime` | `alchemy`, `alchemy::input`, optional `alchemy::input_sdl` | [Shared input boundary](../../../shared/alchemy/docs/input.md) |
| Xbox evidence | Own independently recovered controller and behavior facts consumed by the PC port; static Xbox execution has no product owner | `docs/info/`, `docs/RE/` | relevant claim or RE document | — |
| Asset-free CI | Pin hosted actions and toolchains, reject game/oracle inputs, and map each host to its truthful policy or native-component tier without manufacturing gameplay evidence | `.github/workflows/asset-free.yml`, `tools/ci.py`, `tools/ci_support.py`, `tests/test_ci.py` | `tools/ci.py:main` | [Asset-free continuous verification](../README.md#asset-free-continuous-verification) |
| Structural/style enforcement | Enforce cohesive source-size limits, Python lint, first-party C/C++ formatting/linting, retired static-tool absence, and shipping config/logger ownership | `tools/check_structure.py`, `tools/lint.py`, `tools/x2_source_policy.py`, `tools/verify_source_policy.py`, `.clang-format`, `.clang-tidy` | ctest structure/style/source-policy checks | — |
| Product and subsystem verification | Exercise shipping owners, native/JIT boundaries, launch/setup policy, and independent comparisons without linking test-only execution into gameplay | `tests/` | Focused tests named for the production owner; interpreter-backed diagnostics remain separately linked | `docs/strategy.md` |

## Source tree

Hand-written guest ABI compatibility helpers live under `src/runtime/x86_abi/`;
CPU translation semantics belong only in `shared/x86port`. This tree was produced with
`codemap.py tree src --depth 1 --min-lines 1`.

```text
src/  —  100,132 lines, 495 files
├─ audio/  220 lines  2 files  [.c .h]
├─ config/  1,024 lines  14 files  [.h .c .cpp]
├─ d3d8/  13,851 lines  56 files  [.h .c .cpp]
├─ gen/  23,011 lines  2 files  [.h]
├─ gpu/  7,759 lines  43 files  [.c .h]
├─ input/  2,013 lines  26 files  [.h .c .cpp]
├─ media/  1,111 lines  13 files  [.h .c]
├─ native/  43,137 lines  274 files  [.c .h .cpp]
├─ presentation/  847 lines  13 files  [.h .c]
├─ runtime/  1,720 lines  7 files  [.h .c]
├─ save/  1,142 lines  17 files  [.h .c]
├─ ui/  1,154 lines  12 files  [.cpp .hpp .h .c]
├─ vulkan/  2,206 lines  8 files  [.c .h]

TOTAL: 100,132 lines across 495 files in 1 root(s)
```

Repository-level `tests/` owns focused verification.

## Where does X go?

- **A new native override** → the smallest title/engine subsystem owner in
  `src/native/`, with `x86_register_override` beside its implementation; routing
  support stays in `src/native/x86rt_native.c`, `guest_body.c`, and the
  `x86_engine*` runtime boundary.
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
- **The first shared Alchemy gameplay consumer** → an X-Men 2 guest
  `igControllerManager` adapter over `alchemy_input`, A/B-verified against the
  existing DirectInput path as specified by the
  [shared input boundary](../../../shared/alchemy/docs/input.md).
  Further title-neutral behavior extends the shared owner only after an X-Men 2
  shipping-path contract proves it. MUA is not a current consumer.
- **An x86 decode, semantic, emitter, or cache rule** → canonical
  `shared/x86port`; title-specific CPU forks do not belong here.
- **A binary/asset RE finding** → the smallest Python tool under `tools/` when
  repeatable, with the resulting fact in `docs/RE/` or `docs/info/`; never an
  offline product code generator.
- **An Xbox-derived behavioral fact** → the relevant claim/RE authority. There
  is no Xbox static product owner.
