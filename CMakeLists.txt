cmake_minimum_required(VERSION 3.16)
project(xmen2_sdl LANGUAGES C CXX)

# ONE build tree. `./run.sh` maintains scratch/build-native, and that is the
# tree tools/live_case.py, tools/native_discover.sh and tools/run.py all
# execute. A second in-source tree (the hand-configured `build/`) emitted a
# target of the same name that no harness ever ran: building it produced a
# binary that looked current and was not, and a full live PASS report once
# described an hour-old binary (issue #118). Its bare configure also picked up
# the system interpreter rather than the locked .venv, so tests needing Pillow
# failed for a reason that had nothing to do with the code.
#
# An in-source directory under scratch/ is fine (scratch/ is gitignored and is
# where every run artifact belongs) and so is any tree outside the source root,
# which is what BUILD=<path> in tools/run.py configures. Anything else is the
# mistake above, and it is refused rather than silently allowed.
file(RELATIVE_PATH _x2_build_rel "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
if(NOT _x2_build_rel MATCHES "^\\.\\./" AND NOT _x2_build_rel MATCHES "^scratch/")
    message(FATAL_ERROR
        "Refusing to configure into '${_x2_build_rel}'.\n"
        "This project has ONE build tree: scratch/build-native, maintained by "
        "./run.sh and executed by every live harness. A second in-source tree "
        "builds a same-named x2native that nothing runs (issue #118).\n"
        "  ./run.sh                                    provision, build, launch\n"
        "  cmake --build scratch/build-native -j$(nproc)\n"
        "  ctest --test-dir scratch/build-native --output-on-failure\n"
        "Use a path under scratch/, or one outside the source root (BUILD=...).")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3, for its GPU abstraction: it is the layer that will stand in for D3D8,
# and it maps onto Metal on macOS rather than needing MoltenVK underneath.
find_package(PkgConfig REQUIRED)
pkg_check_modules(X2_FFMPEG REQUIRED IMPORTED_TARGET
                  libavformat libavcodec libavutil libswscale libswresample)
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
endif()
if(TARGET SDL3::SDL3)
    set(X2_SDL SDL3::SDL3)
else()
    set(X2_SDL PkgConfig::SDL3)
endif()

find_package(SDL2 QUIET)          # asset viewers only -- see below
if(NOT SDL2_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
endif()

# The Alchemy engine layer -- the IGB readers, the XMLB container, the ARK
# tooling and the `ig` controller abstraction -- is NOT part of this port. It
# belongs to the engine X-Men Legends II shares with Marvel Ultimate Alliance,
# and lives in the `alchemy` repo (see tools/alchemy_path.py, and the repo
# layout README). Its viewers, dump tools and tests build from there.
#
# x2native itself links none of it, which is why this port needs no CMake
# dependency on it at all.

enable_testing()

add_executable(test_fmv_policy tests/test_fmv_policy.c
               src/media/fmv_policy.c)
target_include_directories(test_fmv_policy PRIVATE src/media)
add_test(NAME fmv_policy COMMAND test_fmv_policy)

add_executable(test_fmv_timing tests/test_fmv_timing.c
               src/media/fmv_timing.c)
target_include_directories(test_fmv_timing PRIVATE src/media)
target_link_libraries(test_fmv_timing PRIVATE m)
add_test(NAME fmv_timing COMMAND test_fmv_timing)

add_executable(test_fmv_probe tests/test_fmv_probe.c src/media/fmv_probe.c)
target_include_directories(test_fmv_probe PRIVATE src/media)
add_test(NAME fmv_probe COMMAND test_fmv_probe)

add_executable(test_fmv_decoder_drain tests/test_fmv_decoder_drain.c
               src/media/fmv_decoder_drain.c)
target_include_directories(test_fmv_decoder_drain PRIVATE src/media)
add_test(NAME fmv_decoder_drain COMMAND test_fmv_decoder_drain)

add_executable(test_d3d8_texture_luma tests/test_d3d8_texture_luma.c
               src/d3d8/d3d8_texture_luma.c)
target_include_directories(test_d3d8_texture_luma PRIVATE src/d3d8)
add_test(NAME d3d8_texture_luma COMMAND test_d3d8_texture_luma)

add_executable(test_movie_audio tests/test_movie_audio.c
               src/audio/movie_audio.c)
target_include_directories(test_movie_audio PRIVATE src/audio)
target_link_libraries(test_movie_audio PRIVATE m pthread)
add_test(NAME movie_audio COMMAND test_movie_audio)

add_executable(test_audio_play_policy tests/test_audio_play_policy.c
               src/audio/audio_play_policy.c)
target_include_directories(test_audio_play_policy PRIVATE src/audio)
add_test(NAME audio_play_policy COMMAND test_audio_play_policy)

add_executable(test_movie_image_layout tests/test_movie_image_layout.c
               src/native/movie_image_layout.c)
target_include_directories(test_movie_image_layout PRIVATE src/native)
add_test(NAME movie_image_layout COMMAND test_movie_image_layout)

add_executable(test_win_path tests/test_win_path.c src/native/win_path.c)
target_include_directories(test_win_path PRIVATE src/native src/save)
target_compile_definitions(test_win_path PRIVATE
    X2_TEST_WIN_PATH_ROOT="${CMAKE_BINARY_DIR}/test-win-path")
add_test(NAME win_path COMMAND test_win_path)

add_executable(test_fmv_decode tests/test_fmv_decode.c
               src/media/fmv_player.c src/media/fmv_audio_decode.c
               src/media/fmv_decoder_drain.c src/media/fmv_policy.c
               src/media/fmv_probe.c src/media/fmv_timing.c)
target_include_directories(test_fmv_decode PRIVATE src/media)
target_link_libraries(test_fmv_decode PRIVATE PkgConfig::X2_FFMPEG m)
add_test(NAME fmv_decode COMMAND test_fmv_decode)
set_tests_properties(fmv_decode PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME structure
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_structure.py)
add_test(NAME structure_selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_structure.py --selftest)
add_test(NAME project_state
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/project_state.py
                 --root ${CMAKE_SOURCE_DIR})
set_tests_properties(project_state PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME codemap
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/codemap.py
                 check --map ${CMAKE_SOURCE_DIR}/docs/codemap.md
                 src tools xbox patches)
set_tests_properties(codemap PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_settings tests/test_settings.c src/config/settings.c
               src/config/input_assignments.c
               src/config/boot_mode.c)
target_include_directories(test_settings PRIVATE src/config)
target_compile_definitions(test_settings PRIVATE
    X2_TEST_SETTINGS_PATH="${CMAKE_BINARY_DIR}/test-x2native.conf")
add_test(NAME settings COMMAND test_settings)

add_executable(test_settings_overlay tests/test_settings_overlay.c
                src/ui/settings_overlay_state.c)
target_include_directories(test_settings_overlay PRIVATE src/ui)
target_link_libraries(test_settings_overlay PRIVATE ${X2_SDL})
add_test(NAME settings_overlay COMMAND test_settings_overlay)

add_executable(test_display_mode_seed tests/test_display_mode_seed.c
                src/presentation/display_mode_seed.c)
target_include_directories(test_display_mode_seed PRIVATE src/presentation
                          src/native src/config)
add_test(NAME display_mode_seed COMMAND test_display_mode_seed)

add_executable(test_boot_mode_policy tests/test_boot_mode_policy.c
               src/native/boot_mode_policy.c)
target_include_directories(test_boot_mode_policy PRIVATE src/config src/native)
add_test(NAME boot_mode_policy COMMAND test_boot_mode_policy)

add_executable(test_boot_mode_runtime tests/test_boot_mode_runtime.c
               src/native/boot_mode_runtime.c src/native/boot_mode_policy.c
               src/save/save_catalog.c src/save/save_directory.c)
target_include_directories(test_boot_mode_runtime PRIVATE src/config src/native
                                                        src/save)
target_compile_definitions(test_boot_mode_runtime PRIVATE
    X2_TEST_BOOT_STORAGE_ROOT="${CMAKE_BINARY_DIR}/test-boot-storage")
add_test(NAME boot_mode_runtime COMMAND test_boot_mode_runtime)

add_executable(test_boot_menu_transition tests/test_boot_menu_transition.c
               src/native/boot_menu_transition.c)
target_include_directories(test_boot_menu_transition PRIVATE src/native src/recomp)
add_test(NAME boot_menu_transition COMMAND test_boot_menu_transition)

add_executable(test_joystick_neutral tests/test_joystick_neutral.c
               src/input/joystick_neutral.c)
target_include_directories(test_joystick_neutral PRIVATE src/input)
add_test(NAME joystick_neutral COMMAND test_joystick_neutral)

add_executable(test_aspect_fit tests/test_aspect_fit.c
               src/presentation/aspect_fit.c)
target_include_directories(test_aspect_fit PRIVATE src/presentation)
add_test(NAME aspect_fit COMMAND test_aspect_fit)

add_executable(test_shadow_policy tests/test_shadow_policy.c
               src/gpu/gpu_matrix.c src/gpu/shadow_policy.c)
target_include_directories(test_shadow_policy PRIVATE src/gpu)
target_link_libraries(test_shadow_policy PRIVATE m)
add_test(NAME shadow_policy COMMAND test_shadow_policy)

add_executable(test_player_input tests/test_player_input.c
               src/input/player_input.c src/input/player_participation_policy.c
               src/config/settings.c src/config/input_assignments.c
               src/config/boot_mode.c
               src/native/input_binding_sets.c
               src/native/xbox_defaults.c)
target_include_directories(test_player_input PRIVATE src/recomp src/native
                                                   src/config src/input)
add_test(NAME player_input COMMAND test_player_input)

add_executable(test_player_participation_policy
               tests/test_player_participation_policy.c
               src/input/player_participation_policy.c)
target_include_directories(test_player_participation_policy PRIVATE src/input)
add_test(NAME player_participation_policy
         COMMAND test_player_participation_policy)

add_executable(test_controller_hotplug tests/test_controller_hotplug.c
               src/input/controller_hotplug.c)
target_include_directories(test_controller_hotplug PRIVATE src/input)
add_test(NAME controller_hotplug COMMAND test_controller_hotplug)

add_executable(test_controller_instance tests/test_controller_instance.c
               src/input/controller_instance.c)
target_include_directories(test_controller_instance PRIVATE src/input src/native)
add_test(NAME controller_instance COMMAND test_controller_instance)

add_executable(test_dinput8_controller_slots
               tests/test_dinput8_controller_slots.c
               src/native/dinput8_controller_slots.c
               src/native/guest_memory.c)
target_include_directories(test_dinput8_controller_slots PRIVATE src/native)
target_link_libraries(test_dinput8_controller_slots PRIVATE pthread)
add_test(NAME dinput8_controller_slots COMMAND test_dinput8_controller_slots)
set_tests_properties(dinput8_controller_slots PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_transient_controller_assignment
               tests/test_transient_controller_assignment.c
               src/input/transient_controller_assignment.c
               src/input/controller_instance.c
               src/config/settings.c src/config/input_assignments.c
               src/config/boot_mode.c)
target_include_directories(test_transient_controller_assignment PRIVATE
                           src/input src/native src/config)
target_compile_definitions(test_transient_controller_assignment PRIVATE
    X2_TEST_TRANSIENT_SETTINGS_PATH="${CMAKE_BINARY_DIR}/test-transient-settings.conf")
add_test(NAME transient_controller_assignment
         COMMAND test_transient_controller_assignment)

add_executable(test_controller_assignment_rows
               tests/test_controller_assignment_rows.cpp
               src/ui/controller_assignment_rows.cpp)
target_include_directories(test_controller_assignment_rows PRIVATE
                           src/ui src/input src/native src/config)
add_test(NAME controller_assignment_rows COMMAND test_controller_assignment_rows)

add_executable(test_input_record tests/test_input_record.c
               src/input/input_record.c src/native/json_string.c)
target_include_directories(test_input_record PRIVATE src/input src/native)
add_test(NAME input_record COMMAND test_input_record)
set_tests_properties(input_record PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_dinput_system tests/test_dinput_system.c
               src/native/dinput_system.c tests/native_stubs.c)
target_include_directories(test_dinput_system PRIVATE src/recomp src/native)
target_compile_definitions(test_dinput_system PRIVATE X2_WITH_SDL)
target_link_libraries(test_dinput_system PRIVATE ${X2_SDL})
add_test(NAME dinput_system COMMAND test_dinput_system)

add_executable(test_cutscene_skip_publication
               tests/test_cutscene_skip_publication.c
               src/native/cutscene_skip_publication.c)
target_include_directories(test_cutscene_skip_publication PRIVATE src/native)
add_test(NAME cutscene_skip_publication COMMAND test_cutscene_skip_publication)

add_executable(test_cutscene_player_policy
               tests/test_cutscene_player_policy.c
               src/native/cutscene_player_policy.c)
target_include_directories(test_cutscene_player_policy PRIVATE src/native)
add_test(NAME cutscene_player_policy COMMAND test_cutscene_player_policy)

add_executable(test_cutscene_dialogue
               tests/test_cutscene_dialogue.c
               src/native/cutscene_dialogue.c
               src/native/guest_memory.c)
target_include_directories(test_cutscene_dialogue PRIVATE
                           src/native src/recomp)
target_compile_definitions(test_cutscene_dialogue PRIVATE X86_NATIVE)
target_link_libraries(test_cutscene_dialogue PRIVATE pthread)
add_test(NAME cutscene_dialogue COMMAND test_cutscene_dialogue)

add_executable(test_behaved_player_heap
               tests/test_behaved_player_heap.c
               src/native/behaved_player.c src/native/guest_memory.c)
target_include_directories(test_behaved_player_heap PRIVATE
                           src/native src/recomp)
target_compile_definitions(test_behaved_player_heap PRIVATE X86_NATIVE)
target_link_libraries(test_behaved_player_heap PRIVATE pthread)
add_test(NAME behaved_player_heap COMMAND test_behaved_player_heap)

add_executable(test_cutscene_event_player
               tests/test_cutscene_event_player.c
               src/native/cutscene_event_player.c src/native/guest_memory.c)
target_include_directories(test_cutscene_event_player PRIVATE
                           src/native src/recomp)
target_compile_definitions(test_cutscene_event_player PRIVATE X86_NATIVE)
target_link_libraries(test_cutscene_event_player PRIVATE pthread)
add_test(NAME cutscene_event_player COMMAND test_cutscene_event_player)

add_test(NAME cutscene_player_wiring
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_cutscene_player_wiring.py)
add_test(NAME cutscene_player_wiring_selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_cutscene_player_wiring.py
                         --selftest)

add_executable(test_binding_rows tests/test_binding_rows.c
               src/input/binding_rows.c)
target_include_directories(test_binding_rows PRIVATE src/input)
add_test(NAME binding_row_labels COMMAND test_binding_rows)

set(X2_PAD_GLYPH_HEADER "${CMAKE_BINARY_DIR}/pad_glyph_codes.h")
add_custom_command(
    OUTPUT "${X2_PAD_GLYPH_HEADER}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            --emit-header "${X2_PAD_GLYPH_HEADER}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            "${CMAKE_SOURCE_DIR}/assets/buttons/glyphs.json"
    COMMENT "x2native: generating button-glyph codepoints")
add_custom_target(x2_pad_glyph_header DEPENDS "${X2_PAD_GLYPH_HEADER}")

# The RGBA prompt atlas is generated from the shared SVG authority. Resolve
# that complete input set at configure time so a plain `cmake --build` sees
# changes to the art itself, not only changes to this repository's generator.
execute_process(
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/render_prompt_glyphs.py" --print-inputs
    RESULT_VARIABLE X2_PROMPT_GLYPH_INPUTS_RESULT
    OUTPUT_VARIABLE X2_PROMPT_GLYPH_INPUTS_TEXT
    ERROR_VARIABLE X2_PROMPT_GLYPH_INPUTS_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT X2_PROMPT_GLYPH_INPUTS_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Refusing prompt-atlas configuration: shared SVG input resolution failed:\n"
        "${X2_PROMPT_GLYPH_INPUTS_ERROR}")
endif()
string(REPLACE "\r\n" "\n" X2_PROMPT_GLYPH_INPUTS_TEXT
       "${X2_PROMPT_GLYPH_INPUTS_TEXT}")
string(REPLACE "\n" ";" X2_PROMPT_GLYPH_INPUTS
       "${X2_PROMPT_GLYPH_INPUTS_TEXT}")
list(FILTER X2_PROMPT_GLYPH_INPUTS EXCLUDE REGEX "^$")
list(LENGTH X2_PROMPT_GLYPH_INPUTS X2_PROMPT_GLYPH_INPUT_COUNT)
if(X2_PROMPT_GLYPH_INPUT_COUNT LESS 1)
    message(FATAL_ERROR
        "Refusing prompt-atlas configuration: resolved 0 shared atlas inputs")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_SOURCE_DIR}/tools/render_prompt_glyphs.py"
             "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
             "${CMAKE_SOURCE_DIR}/tools/shared_dir.py"
             "${CMAKE_SOURCE_DIR}/assets/buttons/glyphs.json"
             ${X2_PROMPT_GLYPH_INPUTS})

set(X2_PROMPT_GLYPH_ATLAS_HEADER
    "${CMAKE_SOURCE_DIR}/src/recomp/gen/prompt_glyph_atlas.h")
add_custom_command(
    OUTPUT "${X2_PROMPT_GLYPH_ATLAS_HEADER}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/render_prompt_glyphs.py"
            "${X2_PROMPT_GLYPH_ATLAS_HEADER}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/render_prompt_glyphs.py"
            "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            "${CMAKE_SOURCE_DIR}/tools/shared_dir.py"
            "${CMAKE_SOURCE_DIR}/assets/buttons/glyphs.json"
            ${X2_PROMPT_GLYPH_INPUTS}
    COMMENT "x2native: rasterising shared prompt SVGs into the native RGBA atlas"
    VERBATIM)
add_custom_target(x2_prompt_glyph_atlas
                  DEPENDS "${X2_PROMPT_GLYPH_ATLAS_HEADER}")


# The gamepad inventory, against an SDL virtual pad. dinput_pad.c depends on
# nothing in the guest runtime -- it is SDL on one side and plain values on the
# other -- so it links straight into a test binary.
add_executable(test_dinput_pad tests/test_dinput_pad.c src/native/dinput_pad.c
        src/native/dinput_pad_virtual.c
               src/native/guest_clock.c)
target_include_directories(test_dinput_pad PRIVATE src/native)
target_compile_definitions(test_dinput_pad PRIVATE X2_WITH_SDL)
target_link_libraries(test_dinput_pad PRIVATE ${X2_SDL})
add_test(NAME dinput_pad COMMAND test_dinput_pad)

add_executable(test_pad_glyphs tests/test_pad_glyphs.c
               src/native/pad_glyphs.c src/native/prompt_glyphs.c
               src/native/prompt_labels.c src/native/input_binding_sets.c
               src/native/input_bindings.c
               src/native/guest_memory.c
               tests/native_stubs.c)
add_dependencies(test_pad_glyphs x2_pad_glyph_header)
target_include_directories(test_pad_glyphs PRIVATE src/recomp src/native
                                                  src/input ${CMAKE_BINARY_DIR})
target_compile_definitions(test_pad_glyphs PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_pad_glyphs PRIVATE pthread)
add_test(NAME pad_glyphs COMMAND test_pad_glyphs)
add_test(NAME pad_glyphs_disabled COMMAND test_pad_glyphs --disabled)

# The glyph-loop detector, against both classes. On the real run it has only
# ever answered zero; this is where it is made to answer one.
add_executable(test_prompt_glyph_draw tests/test_prompt_glyph_draw.c
               src/native/prompt_glyph_draw.c src/native/prompt_glyphs.c
               src/native/prompt_glyph_atlas.c
               src/native/prompt_glyph_metrics.c
               src/native/prompt_glyph_quads.c
               src/native/guest_memory.c
               tests/native_stubs.c)
add_dependencies(test_prompt_glyph_draw x2_pad_glyph_header
                 x2_prompt_glyph_atlas)
target_include_directories(test_prompt_glyph_draw
                           PRIVATE src/recomp src/native src/recomp/gen
                                   ${CMAKE_BINARY_DIR})
target_compile_definitions(test_prompt_glyph_draw PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_prompt_glyph_draw PRIVATE m pthread)
add_test(NAME prompt_glyph_draw COMMAND test_prompt_glyph_draw)

add_executable(test_prompt_glyph_metrics tests/test_prompt_glyph_metrics.c
               src/native/prompt_glyph_metrics.c
               src/native/prompt_glyphs.c
               src/native/prompt_glyph_atlas.c src/native/guest_memory.c
               tests/native_stubs.c)
add_dependencies(test_prompt_glyph_metrics x2_pad_glyph_header
                 x2_prompt_glyph_atlas)
target_include_directories(test_prompt_glyph_metrics
                           PRIVATE src/recomp src/native src/recomp/gen
                                   ${CMAKE_BINARY_DIR})
target_compile_definitions(test_prompt_glyph_metrics PRIVATE _GNU_SOURCE
                                                           X86_NATIVE)
target_link_libraries(test_prompt_glyph_metrics PRIVATE m pthread)
add_test(NAME prompt_glyph_metrics COMMAND test_prompt_glyph_metrics)

add_executable(test_prompt_glyph_batch tests/test_prompt_glyph_batch.c
               src/native/prompt_glyph_batch.c tests/native_stubs.c)
target_include_directories(test_prompt_glyph_batch
                           PRIVATE src/recomp src/native src/gpu)
add_test(NAME prompt_glyph_batch COMMAND test_prompt_glyph_batch)

add_executable(test_ui_transform tests/test_ui_transform.c
               src/native/ui_transform.c src/gpu/gpu_matrix.c
               src/native/guest_memory.c tests/native_stubs.c)
target_include_directories(test_ui_transform
                           PRIVATE src/recomp src/native src/gpu)
target_compile_definitions(test_ui_transform PRIVATE X86_NATIVE)
target_link_libraries(test_ui_transform PRIVATE pthread)
add_test(NAME ui_transform COMMAND test_ui_transform)

add_executable(test_dialog_prompts tests/test_dialog_prompts.c
               src/native/dialog_prompts.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_dialog_prompts PRIVATE src/recomp src/native
                                                       src/input)
target_compile_definitions(test_dialog_prompts PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_dialog_prompts PRIVATE pthread)
add_test(NAME dialog_prompts COMMAND test_dialog_prompts)

add_executable(test_xbox_defaults tests/test_xbox_defaults.c
               src/native/xbox_defaults.c)
target_include_directories(test_xbox_defaults PRIVATE src/native)
add_test(NAME xbox_defaults COMMAND test_xbox_defaults)


# Known-answer tests for the ADC/SBB flag model (tests/test_flags.c). Header-
# only, so it needs no recompiled output and runs in the ordinary test suite --
# the defect it covers was invisible for as long as nothing tested the runtime
# in isolation.
add_executable(test_flags tests/test_flags.c)
target_include_directories(test_flags PRIVATE src/recomp)
add_test(NAME flags COMMAND test_flags)

# The guest printf format walker. It builds crt.c against a stub runtime, so it
# needs no recompiled output and runs in the ordinary suite.
add_executable(test_vformat tests/test_vformat.c src/native/crt.c src/recomp/x87crt.c
            tests/native_stubs.c
               src/native/guest_heap.c src/native/guest_memory.c)
target_include_directories(test_vformat PRIVATE src/recomp src/native)
target_compile_definitions(test_vformat PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_vformat PRIVATE m pthread)
add_test(NAME vformat COMMAND test_vformat)

add_executable(test_x87crt tests/test_x87crt.c src/recomp/x87crt.c)
target_include_directories(test_x87crt PRIVATE src/recomp)
target_link_libraries(test_x87crt PRIVATE m)
add_test(NAME x87crt COMMAND test_x87crt)

# This is deliberately a hardware-x87 oracle: its inline assembly proves the
# ordinary-host-call bridge drains the physical x87 stack. AArch64 has no such
# stack; its translated calls use the model in x86rt.h and are covered by the
# architecture-independent x87 tests above.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i.86)$")
    add_executable(test_x87host tests/test_x87host.c src/recomp/x87host.c)
    target_include_directories(test_x87host PRIVATE src/recomp)
    target_link_libraries(test_x87host PRIVATE m)
    add_test(NAME x87host COMMAND test_x87host)
endif()

add_executable(test_x86callbacks
    tests/test_x86callbacks.c src/recomp/x86callbacks.c
    src/native/guest_memory.c tests/native_stubs.c)
target_include_directories(test_x86callbacks PRIVATE src/recomp src/native)
target_compile_definitions(test_x86callbacks PRIVATE X86_NATIVE)
target_link_libraries(test_x86callbacks PRIVATE pthread)
add_test(NAME x86callbacks COMMAND test_x86callbacks)

# A tail at the current dispatch frame may iterate through the dispatch loop;
# one reached through a direct generated call must finish inline before that
# caller resumes. Reversing those two cases left a guest return address on ESP
# and surfaced later as issue #81's false /GS buffer-overrun report.
add_executable(test_x86_tail_policy
    tests/test_x86_tail_policy.c src/native/x86_tail_policy.c)
target_include_directories(test_x86_tail_policy PRIVATE src/native)
add_test(NAME x86_tail_policy COMMAND test_x86_tail_policy)

add_executable(test_x86watch_memory
    tests/test_x86watch_memory.c src/x86watch_memory.c)
target_include_directories(test_x86watch_memory PRIVATE src)
add_test(NAME x86watch_memory COMMAND test_x86watch_memory)

add_executable(test_x86watch_trace
    tests/test_x86watch_trace.c src/x86watch_trace.c)
target_include_directories(test_x86watch_trace PRIVATE src)
add_test(NAME x86watch_trace COMMAND test_x86watch_trace)

add_executable(test_x86watch_stack
    tests/test_x86watch_stack.c src/x86watch_stack.c)
target_include_directories(test_x86watch_stack PRIVATE src)
add_test(NAME x86watch_stack COMMAND test_x86watch_stack)

# These three tests are x86 hardware oracles: each compares the model against
# instructions executed by the host CPU. They remain valuable on Intel, but
# cannot be compiled on AArch64 (whose translated models use portable C/NEON).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i.86)$")
# The MMX lane model, against the host CPU's own SSE2 (tests/test_mmx.c).
# libCriMovie's video decoder is 1106 MMX instructions and a wrong lane
# produces a picture -- just not the right one -- so the oracle is silicon
# rather than a second reading of the manual. Header-only, like test_flags.
add_executable(test_mmx tests/test_mmx.c)
target_include_directories(test_mmx PRIVATE src/recomp)
add_test(NAME mmx COMMAND test_mmx)

# The rotate model, against the host CPU's own ROL/ROR/RCL/RCR
# (tests/test_rotate.c). RCL and RCR rotate through the carry -- a quantity one
# bit wider than the operand -- which is not a shift pair, and MSVC's 64-bit
# division helpers use RCR, so any guest that divides a long long reaches it.
add_executable(test_rotate tests/test_rotate.c)
target_include_directories(test_rotate PRIVATE src/recomp)
add_test(NAME rotate COMMAND test_rotate)

# The SSE model, against the host CPU's own SSE (tests/test_sse.c). The
# engine's matrix and skinning maths is SSE -- libIGMath's
# alignedMatrixMultiplySSE is on the path any animated character takes -- and
# the parts that go wrong quietly are the lane semantics, not the arithmetic:
# which lanes a scalar op preserves, which operand SHUFPS takes each half
# from, and that MINPS is `dst < src ? dst : src` and not fminf().
add_executable(test_sse tests/test_sse.c)
target_include_directories(test_sse PRIVATE src/recomp)
target_link_libraries(test_sse PRIVATE m)
add_test(NAME sse COMMAND test_sse)
endif()

# Region recording's arm is a diagnostic discriminator: an address outside the
# emitted ranges, one that was compiled but never reached, and one that fired
# must produce three different answers.  Exercise those answers without a game
# install or GPU so the diagnostic cannot silently regress between captures.
add_executable(test_x86_record tests/test_x86_record.c src/native/x86_record.c)
target_include_directories(test_x86_record PRIVATE src/recomp src/native)
add_test(NAME x86_record_arm_invalid COMMAND test_x86_record invalid)
set_tests_properties(x86_record_arm_invalid PROPERTIES
    PASS_REGULAR_EXPRESSION "X2_RECORD_ARM was INVALID")
add_test(NAME x86_record_arm_missing COMMAND test_x86_record missing)
set_tests_properties(x86_record_arm_missing PROPERTIES
    PASS_REGULAR_EXPRESSION "arm 0x00002000 was NOT COMPILED IN")
add_test(NAME x86_record_arm_never_reached COMMAND test_x86_record never)
set_tests_properties(x86_record_arm_never_reached PROPERTIES
    PASS_REGULAR_EXPRESSION "arm 0x00001004 NEVER EXECUTED")
add_test(NAME x86_record_arm_capture COMMAND test_x86_record capture)
set_tests_properties(x86_record_arm_capture PROPERTIES
    PASS_REGULAR_EXPRESSION "2 entries captured over 1 pass")

# The native executable, not only run.sh, loads the project .env. These tests
# link the exact shipping parser and prove both precedence directions plus its
# refusal of malformed input and executable-project precedence.
add_executable(test_env_file tests/test_env_file.c src/native/env_file.c)
target_include_directories(test_env_file PRIVATE src/native)
add_test(NAME env_file_load COMMAND test_env_file load)
add_test(NAME env_file_preserve COMMAND test_env_file preserve)
add_test(NAME env_file_malformed COMMAND test_env_file malformed)
add_test(NAME env_file_executable_precedence
    COMMAND test_env_file executable-precedence)

add_executable(test_x2native_options
    tests/test_x2native_options.c src/native/x2native_options.c)
target_include_directories(test_x2native_options PRIVATE src/native)
add_test(NAME x2native_options COMMAND test_x2native_options)

add_executable(test_options_menu
    tests/test_options_menu.c src/native/options_menu.c
    src/native/guest_heap.c src/native/guest_memory.c
    src/ui/settings_overlay_state.c
    tests/options_menu_stubs.c)
target_include_directories(test_options_menu PRIVATE src/recomp src/native src/ui)
target_compile_definitions(test_options_menu PRIVATE X86_NATIVE)
target_link_libraries(test_options_menu PRIVATE pthread)
add_test(NAME options_menu COMMAND test_options_menu)

add_executable(test_save_catalog
    tests/test_save_catalog.c src/save/save_catalog.c)
target_include_directories(test_save_catalog PRIVATE src/save)
add_test(NAME save_catalog COMMAND test_save_catalog)

add_executable(test_save_directory
    tests/test_save_directory.c src/save/save_directory.c)
target_include_directories(test_save_directory PRIVATE src/save src/native)
add_test(NAME save_directory COMMAND test_save_directory)

add_executable(test_autosave_policy
    tests/test_autosave_policy.c src/save/autosave_policy.c)
target_include_directories(test_autosave_policy PRIVATE src/save)
add_test(NAME autosave_policy COMMAND test_autosave_policy)

add_executable(test_continue_policy
    tests/test_continue_policy.c src/save/continue_policy.c)
target_include_directories(test_continue_policy PRIVATE src/save)
add_test(NAME continue_policy COMMAND test_continue_policy)

add_executable(test_autosave_storage
    tests/test_autosave_storage.c src/save/autosave_storage.c
    src/save/save_directory.c)
target_include_directories(test_autosave_storage PRIVATE src/save src/native)
add_test(NAME autosave_storage COMMAND test_autosave_storage)
set_tests_properties(autosave_storage PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_autosave_format
    tests/test_autosave_format.c src/save/autosave_format.c)
target_include_directories(test_autosave_format PRIVATE src/save)
add_test(NAME autosave_format COMMAND test_autosave_format)

add_executable(test_save_trace
    tests/test_save_trace.c src/save/save_trace.c)
target_include_directories(test_save_trace PRIVATE src/save)
add_test(NAME save_trace COMMAND test_save_trace)

add_executable(test_shadow_trace
    tests/test_shadow_trace.c tools/proxy_d3d8/shadow_trace.c)
target_include_directories(test_shadow_trace PRIVATE tools/proxy_d3d8)
add_test(NAME shadow_trace COMMAND test_shadow_trace)
set_tests_properties(shadow_trace PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME shadow_trace_compare COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/shadow_trace_compare.py --selftest)
add_test(NAME build_stocklog COMMAND ${Python3_EXECUTABLE} -m unittest
         tests.test_build_stocklog)
set_tests_properties(build_stocklog PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME oracle_compare_tool COMMAND ${Python3_EXECUTABLE} -m unittest
         tests.test_oracle_compare)
set_tests_properties(oracle_compare_tool PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})



# The lift pipeline's step guard. Every Ghidra step appends to one log and then
# greps the WHOLE file, so a step that DIED still printed the previous run's
# lines and read as success -- which is how a Jython SyntaxError announced
# "recreate 42 function bodies" and repaired none. This runs the guard's own
# self-test, which needs neither Ghidra nor the game install.
add_test(NAME lift_step_guard
         COMMAND ${CMAKE_SOURCE_DIR}/tools/ghidra_export.sh --selftest)

# Native overrides. An override is keyed on (module, entry point at the
# module's PREFERRED base), never a bare address: every libIG*.dll in this game
# is linked for 0x10000000, so one linked address is a real function in eight
# different modules while dispatch works in mapped addresses. Keyed on the
# address alone, an override for a relocated module could never fire AND could
# fire for whichever unrelated module kept the preferred base -- both silent,
# because a missing override just means the recompiled body answers and the run
# looks healthy. The selftest proves the emitter-side gates REJECT a bare
# address, an entry point the named module lacks, a missing source tree, and
# another module's registration -- paths that never run during a normal emit,
# because every registration in the tree is correct.
add_test(NAME overrides COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/recomp_overrides.py --selftest)
add_test(NAME save_trace_wiring COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_save_trace_wiring.py)
set_tests_properties(save_trace_wiring PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME save_trace_wiring_selftest COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_save_trace_wiring.py --selftest)
add_test(NAME continue_wiring COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_continue_wiring.py)
set_tests_properties(continue_wiring PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME continue_wiring_selftest COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_continue_wiring.py --selftest)
add_test(NAME autosave_wiring COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_autosave_wiring.py)
add_test(NAME autosave_wiring_selftest COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_autosave_wiring.py --selftest)

# The guest stack-effect check (tools/stackcheck.py over an X2_STACKCHECK
# record). A guest call must return with esp raised by 4 plus the callee's RET
# immediate; nothing checked that before, and a single dword of drift inside a
# call tree surfaces as memory corruption somewhere unrelated -- FUN_0046b750's
# /GS epilogue read a slot four bytes below the cookie it had stored and
# reported a buffer overrun that never happened. The selftest is what makes the
# tool's usual answer ("every checked call is balanced", over a million calls)
# worth anything: it shows the checker flagging a 4-byte drift and excluding a
# tail-calling body, neither of which a normal run exercises.
add_test(NAME stackcheck COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/stackcheck.py --selftest)

# The oracle probe harness (src/oracle, tools/gen_probes.py, tools/oraclediff.py).
#
# Every way this harness can break produces the SAME artifact: an empty or
# short stream. And an empty stream compared against a full one reads as "no
# differences" unless something refuses it. So both halves carry their
# refusals as tests -- the generator must reject an unhookable prologue, an
# ambiguous name and an argument past the end of the frame; the comparator must
# reject a mismatched manifest, a same-side pair and an empty capture, and must
# distinguish a differing OUTPUT (a defect) from a differing INPUT (two runs
# that simply drifted apart).
add_test(NAME probes_generator COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/gen_probes.py --selftest)
add_test(NAME probes_current COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/gen_probes.py --check)
add_test(NAME oraclediff COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/oraclediff.py --selftest)

# The staleness gate's own discriminator: it must read a current stamp as
# current, a wrong one as stale, an ABSENT one as stale, and the fingerprint
# must move when the translator is edited. A checker only ever run against the
# good case is not a checker.
add_test(NAME emitted_stamp COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/check_emitted.py --selftest)


# The host D3D8 interface tables, against a real d3d8.h and against the game's
# own call sites. Runs the checker's SELF-TEST, not the check: the check needs
# scratch/recomp/libIGGfx.json, which is generated and gitignored, while the
# self-test proves the checks can fail and is meaningful with or without it.
add_test(NAME d3d8_abi
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/d3d8_abi_check.py --selftest)


# The port's own button glyphs: the coordinate round trip (and that it is NOT
# the top-down identity, which is the bug C171 found in the Xbox-art tool), the
# band finder on an image whose longest blank run is not its first, and a blit
# that must touch exactly its own cell.
if(SDL3_FOUND OR TARGET SDL3::SDL3)
    add_executable(test_virtual_pad tests/test_virtual_pad.c)
    target_link_libraries(test_virtual_pad PRIVATE SDL3::SDL3)
    add_test(NAME virtual_pad COMMAND test_virtual_pad)
    # A machine with no gamepad subsystem SKIPs (77) rather than failing, and
    # rather than passing having checked nothing.
    set_tests_properties(virtual_pad PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

add_executable(test_control_png tests/test_control_png.c src/native/control_png.c)
target_include_directories(test_control_png PRIVATE src/native)
target_compile_definitions(test_control_png PRIVATE
    CHECK_PNG="${CMAKE_CURRENT_SOURCE_DIR}/tools/check_png.py")
add_test(NAME control_png COMMAND test_control_png)

add_executable(test_control_screenshot
    tests/test_control_screenshot.c
    src/native/control_screenshot.c
    src/native/control_png.c)
target_include_directories(test_control_screenshot PRIVATE src/native src/gpu)
add_test(NAME control_screenshot COMMAND test_control_screenshot)
add_test(NAME window_capture_wiring
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_window_capture_wiring.py)

add_test(NAME pad_glyph_manifest
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py --selftest)

# The two headers bootstrap.py publishes at every provision. Both carry a
# selftest and neither was gated: the ratio one covers the refusals that keep a
# missing font from silently becoming 1.0, and the atlas one proves every
# published codepoint is actually inked -- an all-blank atlas is exactly the
# uniform output an unvalidated rasteriser produces.
add_test(NAME font_tier_ratio
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/font_tier_ratio.py --selftest)
set_tests_properties(font_tier_ratio PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME prompt_glyph_atlas
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/render_prompt_glyphs.py --selftest
                 --cc ${CMAKE_C_COMPILER})
set_tests_properties(prompt_glyph_atlas PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME prepare_native_assets
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/prepare_native_assets.py --selftest)

add_test(NAME port_pause_menu
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/make_port_pause_menu.py --selftest)

add_test(NAME launcher
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher.py)
set_tests_properties(launcher PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME capture_recomp_metadata
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/capture_recomp_metadata.py --selftest)
set_tests_properties(capture_recomp_metadata PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


# The 42 binding-row storage keys and display labels in
# src/input/binding_rows.c are read out of XMen2.exe and igct.bnx; this diffs
# the shipped table against both sources rather than trusting that they still
# agree. SKIPs loudly (77) without an install, because a check that had nothing
# to read must not look like a check that passed.
add_test(NAME binding_rows
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/binding_rows.py --selftest)
set_tests_properties(binding_rows PROPERTIES SKIP_RETURN_CODE 77
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# XMen2.exe's 289-entry BehavEd script-command table. Reading its four fields
# one dword out pairs every name with the previous command's handler -- the
# names still look right and every override registered from them is silently
# dead. The selftest pins two handlers verified at RUNTIME and eight argument
# specs taken from the shipped scripts, so neither check is the tool marking
# its own work. SKIPs loudly (77) without an install.
# Python lint. Everything but run.sh is Python, and the selection in
# ruff.toml is defects rather than preferences -- see the reasoning there for
# what is deliberately NOT enabled. SKIPs (77) when ruff is not installed,
# because a linter that silently did not run must not read as a clean tree.
add_test(NAME python_lint
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/lint.py)
set_tests_properties(python_lint PROPERTIES SKIP_RETURN_CODE 77
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME x2ctl COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_x2ctl.py)
set_tests_properties(x2ctl PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME script_commands
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/script_commands.py --selftest)
set_tests_properties(script_commands PROPERTIES SKIP_RETURN_CODE 77
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME extract_fb
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/extract_fb.py --selftest)

# The oracle cache, proved to MISS as well as hit -- with a stub runner in
# place of Wine, because what is under test is the keying, not the game. A
# cache that has only ever been shown to hit is a cache that will serve a frame
# captured from a binary that has since been rebuilt.
add_test(NAME oracle_cache
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/oracle.py --selftest)


# ---------------------------------------------------------------------------
# x2native -- recompiled game code as a NATIVE binary (rc-native)
#
# The first artefact that is not hosted by Wine. It needs the generated
# recompiler output, which is gitignored, so the target only exists once
# `tools/recomp.py emit` and `tools/recomp.py native` have been run:
#
#   ${Python3_EXECUTABLE} tools/recomp.py emit   scratch/recomp/libIGDisplay.json \
#                                  src/recomp/libIGDisplay.c
#   ${Python3_EXECUTABLE} tools/recomp.py native scratch/recomp/libIGDisplay.json \
#                                  src/recomp/libIGDisplay_native.c
#
# Configured without them, CMake SAYS so rather than quietly omitting the
# target -- a missing native build that looks like a clean configure is how
# "the native path does not build" turns into "there is no native path".
# ---------------------------------------------------------------------------
# Each recompiled module contributes two generated files and is optional: a
# module that has not been exported yet simply is not linked, and x2native says
# which ones it has at run time rather than pretending to a full set.
#
# cg and cgD3D8 are the NVIDIA Cg runtime shipped in the install, not Alchemy
# modules -- libIGGfx loads them by name and the engine's library loader
# reports a MODAL WARNING when it cannot (issue #45). They recompile like any
# other guest module; what makes them different is that they link the MSVC CRT
# STATICALLY, so their imports are the CRT's own startup surface (heap, std
# handles, locale, code pages) rather than MSVCR71.
#
# msdia80 is Microsoft's Debug Interface Access DLL, shipped beside the game and
# loaded by the engine's directory sweep. It reads PDB symbols, and the engine
# uses it to symbolicate the stack trace it puts in an error box -- the path
# this port keeps landing on.
#
# Stated as measured, because the two do not agree: NOTHING in the shipped
# modules references it by name (no "msdia" string, no DIA CLSID, in any of the
# 18 DLLs or the exe), so the call path is not visible statically. It is
# recompiled rather than merely mapped so that whatever reaches it RUNS instead
# of stopping. It lifts like any other guest module -- 4824 functions, 100% of
# instructions, and only KERNEL32 imports, since its CRT is static.
set(X2_MODULES libIGDisplay libIGCore libIGSg libIGMath libIGAttrs
               libIGGfx libIGUtils libIGGui libIGLua libIGOpt
               libMovie libCriMovie libIGAudio libIGCollision
               libIGInsight libIGViewer
               cg cgD3D8 msdia80 XMen2)
set(X2_MODULE_SRCS "")
set(X2_MODULE_HAVE "")
foreach(m ${X2_MODULES})
    # A module is emitted either as one file or, for the big ones, as numbered
    # chunks (tools/recomp.py emit --split). Globbing the chunks explicitly by
    # digits so that <m>_native.c is never swept in with them.
    set(_b ${CMAKE_SOURCE_DIR}/src/recomp/${m}.c)
    set(_n ${CMAKE_SOURCE_DIR}/src/recomp/${m}_native.c)
    # CONFIGURE_DEPENDS so the glob is re-checked at BUILD time. Without it the
    # chunk list is frozen at configure time, and re-emitting with a different
    # --split silently builds a stale set -- or fails on a chunk that no longer
    # exists, which is how this was found.
    file(GLOB _chunks CONFIGURE_DEPENDS
         ${CMAKE_SOURCE_DIR}/src/recomp/${m}_[0-9][0-9][0-9].c)
    if(EXISTS ${_b})
        set(_bodies ${_b})
    else()
        set(_bodies ${_chunks})
    endif()
    if(_bodies AND EXISTS ${_n})
        list(LENGTH _bodies _nb)
        list(APPEND X2_MODULE_SRCS ${_bodies} ${_n})
        list(APPEND X2_MODULE_HAVE "${m}(${_nb} TU)")
    else()
        message(STATUS "x2native: ${m} NOT linked -- run tools/ghidra_export.sh "
                       "${m} then tools/recomp.py emit/native for it")
    endif()
endforeach()
if(X2_MODULE_HAVE)
    # Dusklight's exact RmlUi revision, including its maintained SDL3/SDL_GPU
    # backend. Pin both the commit and archive digest: a UI dependency update
    # is a reviewed source change, never whatever GitHub serves that day.
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
    set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
    FetchContent_Declare(rmlui
        URL https://github.com/mikke89/RmlUi/archive/f9b8c9e2935d5df2c7dff2c190d3968e99b0c3dc.tar.gz
        URL_HASH SHA256=42b830abc2509a8c07cf02ba3313505de3a4642725654f489402022d8fefdd79
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(rmlui)
    find_package(SDL3_image REQUIRED)
    set_target_properties(rmlui_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(x2_rmlui_backend STATIC
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_SDL_GPU.cpp)
    target_include_directories(x2_rmlui_backend PUBLIC
        ${rmlui_SOURCE_DIR}/Backends)
    target_compile_definitions(x2_rmlui_backend PUBLIC
        RMLUI_SDL_VERSION_MAJOR=3)
    set_target_properties(x2_rmlui_backend PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(x2_rmlui_backend PUBLIC RmlUi::Core ${X2_SDL}
                                                SDL3_image::SDL3_image)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ui")
    configure_file(
        "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
        "${CMAKE_BINARY_DIR}/ui/LatoLatin-Regular.ttf" COPYONLY)
    configure_file(
        "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf"
        "${CMAKE_BINARY_DIR}/ui/LatoLatin-Bold.ttf" COPYONLY)
    configure_file("${CMAKE_SOURCE_DIR}/assets/ui/settings.rcss"
                   "${CMAKE_BINARY_DIR}/ui/settings.rcss" COPYONLY)
    add_library(x2_rmlui_ui STATIC
        src/ui/controller_assignment_rows.cpp
        src/ui/rmlui_ui.cpp
        src/ui/settings_document.cpp
        src/ui/settings_overlay_state.c)
    target_include_directories(x2_rmlui_ui PRIVATE src/native src/config
                                                   src/presentation src/input
                                                   src/gpu)
    target_compile_definitions(x2_rmlui_ui PRIVATE
        X2_UI_FONT_PATH="${CMAKE_BINARY_DIR}/ui/LatoLatin-Regular.ttf"
        X2_UI_FONT_BOLD_PATH="${CMAKE_BINARY_DIR}/ui/LatoLatin-Bold.ttf"
        X2_UI_DOCUMENT_URL="${CMAKE_BINARY_DIR}/ui/settings.rml")
    set_target_properties(x2_rmlui_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(x2_rmlui_ui PUBLIC x2_rmlui_backend)

    # ---- shaders -------------------------------------------------------
    #
    # SDL_GPU takes SPIR-V, so the fixed-function shaders are compiled at build
    # time and embedded as C arrays. glslc is REQUIRED rather than optional: a
    # build that silently had no shaders would link, run, and draw nothing --
    # which is the single hardest rendering failure to attribute.
    find_program(X2_GLSLC NAMES glslc)
    if(NOT X2_GLSLC)
        message(FATAL_ERROR
            "x2native: no glslc found, so the renderer's "
            "shaders cannot be built. Install shaderc/glslang. Refusing rather "
            "than building a binary that would run and draw nothing.")
    endif()
    set(X2_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${X2_SHADER_DIR})
    set(X2_SHADER_HEADERS "")
    foreach(shader d3d8_fixed shadow_depth)
      foreach(st vert frag)
        set(_src ${CMAKE_SOURCE_DIR}/src/gpu/shaders/${shader}.${st})
        set(_out ${X2_SHADER_DIR}/${shader}_${st}.inc)
        # glslc -mfmt=c emits exactly a braced initialiser, so the .inc is
        # #included as one rather than assembled by echo and cat -- which is
        # shell redirection inside a custom command, and it silently produced a
        # malformed header for one of the two stages.
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${X2_GLSLC} -fshader-stage=${st} ${_src} -mfmt=c -o ${_out}
            DEPENDS ${_src}
            COMMENT "x2native: compiling ${shader}.${st} to SPIR-V")
        list(APPEND X2_SHADER_HEADERS ${_out})
      endforeach()
    endforeach()
    add_custom_target(x2_shaders DEPENDS ${X2_SHADER_HEADERS})

    message(STATUS "x2native: linking recompiled modules: ${X2_MODULE_HAVE}")
    add_executable(x2native
        src/native/x2native.c
        src/native/guest_memory.c
        src/native/x2native_options.c
        src/config/settings.c
        src/config/input_assignments.c
        src/config/settings_store.c
        src/config/boot_mode.c
        src/native/boot_mode_policy.c
        src/native/boot_mode_runtime.c
        src/native/boot_menu_transition.c
        src/native/boot_splash_policy.c
        src/presentation/aspect_fit.c
        src/presentation/boot_blackout.c
        src/presentation/window_settings.c
        src/presentation/display_mode_seed.c
        src/native/ui_text_scale.c
        src/native/text_caller_probe.c
        src/native/prompt_glyph_draw.c
        src/native/prompt_glyph_atlas.c
        src/native/prompt_glyph_batch.c
        src/native/prompt_glyph_metrics.c
        src/native/prompt_glyph_quads.c
        src/native/ui_transform.c
        src/input/binding_rows.c
        src/input/controller_hotplug.c
        src/input/controller_instance.c
        src/input/input_probe_lifecycle.c
        src/input/joystick_neutral.c
        src/input/player_input.c
        src/input/player_participation_policy.c
        src/input/transient_controller_assignment.c
        src/input/input_record.c
        src/save/save_catalog.c
        src/save/save_directory.c
        src/save/autosave_policy.c
        src/save/autosave_storage.c
        src/save/autosave_format.c
        src/save/continue_policy.c
        src/save/save_trace.c
        src/native/autosave_runtime.c
        src/native/boot_player_selection.c
        src/native/continue_runtime.c
        src/native/save_trace_runtime.c
        src/native/pe_map.c
        src/native/win32_sdl.c
        src/native/guest_heap.c
        src/native/crt.c
        src/native/crt_selftest.c
        src/recomp/x87crt.c
        src/native/kernel32.c
        src/native/win_path.c
        src/native/advapi32.c
        src/native/advapi32_host_store.c
        src/native/fault_selftest.c
        src/native/ole32.c
        src/native/gdi32.c
        src/native/dinput.c
        src/native/dinput8.c
        src/native/dinput8_controller_slots.c
        src/native/dinput8_hotplug.c
        src/native/dinput_device.c
        src/native/dinput_device_registry.c
        src/native/dinput_device_state.c
        src/native/dinput_system.c
        src/native/dinput_fifo.c
        src/native/dinput_script.c
        src/native/dinput_joystick.c
        src/native/dinput_pad.c
        src/native/dinput_pad_virtual.c
        src/native/dialog_prompts.c
        src/native/pad_glyphs.c
        src/native/prompt_glyphs.c
        src/native/prompt_labels.c
        src/native/input_binding_sets.c
        src/native/input_bindings.c
        src/native/player_participation.c
        src/native/player_participation_probe.c
        src/native/cutscene_skip_publication.c
        src/native/cutscene_skip_probe.c
        src/native/behaved_player.c
        src/native/cutscene_event_player.c
        src/native/conversation_player.c
        src/native/cutscene_dialogue.c
        src/native/cutscene_player_policy.c
        src/native/cutscene_player.c
        src/native/input_probe.c
        src/native/xbox_defaults.c
        src/native/dsound.c
        src/audio/audio_play_policy.c
        src/audio/movie_audio.c
        src/media/fmv_audio_decode.c
        src/media/fmv_decoder_drain.c
        src/media/fmv_player.c
        src/media/fmv_policy.c
        src/media/fmv_probe.c
        src/media/fmv_timing.c
        src/native/guest_clock.c
        src/native/control.c
        src/native/control_png.c
        src/native/control_query.c
        src/native/control_screenshot.c
        src/native/control_status.c
        src/native/json_string.c
        src/native/live_session.c
        src/native/ws2_32.c
        src/native/advapi32.c
        src/native/shell32.c
        src/native/winmm.c
        src/native/threads.c
        src/native/x86_tail_policy.c
        src/native/x86_reached.c
        src/native/run_reports.c
        src/native/x86rt_native.c
        src/native/x86_record.c
        src/native/env_file.c
        src/native/startup.c
        src/native/movie.c
        src/native/movie_image_layout.c
        src/native/options_menu.c
        src/native/reportbox.c
        src/native/security.c
        src/native/conversation.c
        src/native/script_trace.c
        src/native/heartbeat.c
        src/native/oracle_trace.c
        src/recomp/gen/probe_wraps.c
        src/d3d8/d3d8_com.c
        src/d3d8/d3d8_caps.c
        src/d3d8/d3d8_state.c
        src/d3d8/d3d8_stateblock.c
        src/d3d8/d3d8_d3d8.c
        src/d3d8/d3d8_device.c
        src/d3d8/d3d8_lightlog.c
        src/d3d8/d3d8_surface.c
        src/d3d8/d3d8_resource.c
        src/d3d8/d3d8_texture_luma.c
        src/d3d8/d3d8_drawcall.c
        src/d3d8/d3d8_vertex_shader.c
        src/d3d8/d3d8_screen_space_test.c
        src/d3d8/d3d8_light_selftest.c
        src/d3d8/d3d8_report.c
        src/vulkan/igvk_ark.c
        src/vulkan/igvk_probe.c
        src/vulkan/igvk_context.c
        src/gpu/gpu_device.c
        src/gpu/gpu_capture.c
        src/gpu/gpu_draw.c
        src/gpu/gpu_draw_trace.c
        src/gpu/gpu_frame_timing.c
        src/gpu/gpu_shadow.c
        src/gpu/gpu_shadow_selftest.c
        src/gpu/shadow_policy.c
        src/gpu/gpu_frame_submit.c
        src/gpu/gpu_present.c
        src/gpu/gpu_present_selftest.c
        src/gpu/gpu_prompt_glyphs.c
        src/gpu/gpu_host_selftest.c
        src/gpu/gpu_matrix.c
        src/gpu/gpu_frame_init_selftest.c
        src/gpu/gpu_upload.c
        src/gpu/gpu_upload_selftest.c
        src/gpu/gpu_selftest.c
        src/vulkan/igvk_slots_lifecycle.c
        src/vulkan/igvk_slots_frame.c
        
        ${X2_MODULE_SRCS}
    )
    add_dependencies(x2native x2_shaders x2_pad_glyph_header
                     x2_prompt_glyph_atlas)
    target_include_directories(x2native PRIVATE src/recomp src/native src/vulkan
                                                src/config src/presentation src/ui
                                                src/input src/save src/audio
                                                src/media
                                              src/gpu src/d3d8
                                              src/oracle src/recomp/gen
                                              ${CMAKE_BINARY_DIR})
    target_link_libraries(x2native PRIVATE x2_rmlui_ui)

    # Native overrides are declared in C where they belong: x86_register_override
    # in the subsystem files, and recomp.py routes every call (direct or
    # indirect) to a registered entry point through the dispatcher's override
    # slot, which x86_native_call_at consults before the recompiled body. No
    # generated wiring, no --wrap, no --isolate; the C is the single source of
    # truth.

    # Oracle probes: --wrap, for recording rather than replacing. Generated from
    # tools/probes.json by tools/gen_probes.py, which also writes the --isolate
    # lists -- the ONLY remaining user of the isolate mechanism. Missing is
    # FATAL for the same reason: a build without these flags records nothing
    # and reads as a port that agrees with the control.
    set(RECOMP_PROBES_CMAKE
        "${CMAKE_SOURCE_DIR}/src/recomp/gen/recomp_probes.cmake")
    if(NOT EXISTS "${RECOMP_PROBES_CMAKE}")
        message(FATAL_ERROR
            "Missing ${RECOMP_PROBES_CMAKE}.\n"
            "It is generated by tools/gen_probes.py from tools/probes.json. "
            "Building without it would link a binary in which every oracle "
            "probe is silently absent. Run tools/gen_probes.py.")
    endif()
    include("${RECOMP_PROBES_CMAKE}")
    if(NOT APPLE)
        target_link_options(x2native PRIVATE ${RECOMP_PROBE_WRAPS})
    endif()

    # The recompiled C must have been emitted by the translator in THIS tree.
    #
    # src/recomp/*.c is generated and gitignored, so nothing tracked ever shows
    # that a module has fallen behind tools/recomp.py -- and a stale module
    # compiles, links, runs and is wrong in whatever way the fix it missed was
    # about to correct. libIGSg sat two translator fixes behind for days and
    # the port drew warped characters the whole time (issue #80); it came right
    # only when the module was re-emitted for an unrelated reason.
    execute_process(
        # --root explicitly: the translator is a SHARED repo now, so the tool
        # can neither assume its own location is the port nor assume the
        # working directory is (CMake runs it from the build tree).
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/check_emitted.py"
                --root "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE X2_EMIT_STALE
        OUTPUT_VARIABLE X2_EMIT_OUT
        ERROR_VARIABLE  X2_EMIT_ERR)
    if(NOT X2_EMIT_STALE EQUAL 0)
        message(FATAL_ERROR
            "Recompiled C is stale.\n${X2_EMIT_ERR}${X2_EMIT_OUT}")
    endif()
    message(STATUS "x2native: ${X2_EMIT_OUT}")
    target_compile_definitions(x2native PRIVATE X2_WITH_SDL _GNU_SOURCE)
    # -DX2_NATIVE_TRACE=ON traces every recompiled body into the boundary ring.
    # Off by default: it costs a ring write per call, and the generated bodies
    # are otherwise IDENTICAL between the two builds, so what a traced run shows
    # applies to an untraced one.
    if(X2_NATIVE_TRACE)
        target_compile_definitions(x2native PRIVATE X86_NATIVE_TRACE)
    endif()
    # -DX2_DCHECK=ON checks that every guest-to-guest DIRECT call returns the
    # esp its callee's own RET promises. Off by default and free when off: the
    # emitted call is the same plain call either way (X86_DCALL_CHECK expands
    # to nothing), so this is a rebuild, not a re-emit. It is the one boundary
    # tools/stackcheck.py cannot reach -- that checks calls going THROUGH the
    # dispatcher, and a direct call by definition does not.
    if(X2_DCHECK)
        target_compile_definitions(x2native PRIVATE X2_DCHECK)
    endif()
    # -DX2_NATIVE_REACHED=ON records WHICH bodies were ever entered -- a set,
    # not a history, so it answers "was this function ever reached?" which the
    # ring cannot: the ring evicts, so a startup call is gone by the time
    # anything fails. Mutually exclusive with the trace: both claim
    # X86_ENTER_FN, and a build that silently honoured only one would report a
    # ring while the caller believed they had a reached set.
    # The reached set is compiled into EVERY native build (X86_NATIVE) behind
    # a runtime-armed flag, so "does the engine ever call this?" is a question
    # this binary can answer -- it used to need a second 272MB build tree, and
    # a diagnostic that costs a rebuild is a diagnostic nobody runs.
    target_compile_definitions(x2native PRIVATE X86_NATIVE)
    if(X2_NATIVE_REACHED)
        if(X2_NATIVE_TRACE)
            message(FATAL_ERROR
                "x2native: X2_NATIVE_TRACE and X2_NATIVE_REACHED both use the "
                "body-entry hook and cannot be enabled together. Pick one.")
        endif()
        target_compile_definitions(x2native PRIVATE X86_NATIVE_REACHED)
    endif()
    # PIE, so this binary does not sit at 0x400000 -- which is exactly where
    # XMen2.exe wants to be. A non-PIE host takes the guest's preferred base
    # and forces it to relocate for no reason.
    set_target_properties(x2native PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT APPLE)
        target_link_options(x2native PRIVATE -pie)
    endif()
    # libm: the exe's x87 translations use atan2l/fmodl and friends.
    # -pthread: the heartbeat (src/native/heartbeat.c) runs on its own thread
    # so that a guest stuck anywhere still gets a liveness line.
    target_link_libraries(x2native PRIVATE ${X2_SDL} PkgConfig::X2_FFMPEG
                                           m pthread)
    # No $ENV{} here: CMake would bake in whatever GAME_PC_DIR was at CONFIGURE
    # time, which is how this test first "failed" against an empty path. x2native
    # reads it itself at run time and exits 77 (ctest SKIP) if it is unset,
    # saying so -- a skip that announces itself, not a silent pass.
    # -DX2_NATIVE_O0=<file> compiles ONE generated chunk at -O0, so addr2line
    # names the exact guest instruction in it. At -O2 the line attribution
    # inside a 200k-line translated body is approximate, and reading a fault as
    # being at the wrong instruction is worse than admitting it is unknown --
    # a nearby line looks like an answer. Whole-build -O0 is not an option:
    # 2M lines of translated code, so this is per-chunk and opt-in.
    if(X2_NATIVE_O0)
        if(NOT EXISTS ${CMAKE_SOURCE_DIR}/src/recomp/${X2_NATIVE_O0})
            message(FATAL_ERROR "X2_NATIVE_O0: no such generated chunk: "
                                "src/recomp/${X2_NATIVE_O0}")
        endif()
        message(STATUS "x2native: ${X2_NATIVE_O0} at -O0 for exact fault lines")
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/src/recomp/${X2_NATIVE_O0}
            PROPERTIES COMPILE_OPTIONS "-O0")
    endif()
    add_test(NAME native_battery
             COMMAND x2native --no-window --selftest)
    set_tests_properties(native_battery PROPERTIES SKIP_RETURN_CODE 77)
    # The renderer's HOST half, driven with no engine and no game install.
    # It needs neither, which is the point: the game has never reached a
    # frame, so without this nothing would ever have run that code.
    add_test(NAME vk_frame_path
             COMMAND x2native --no-window --vk-selftest)
    set_tests_properties(vk_frame_path PROPERTIES SKIP_RETURN_CODE 77)
    # The host D3D8's ABI tables, vtable dispatch and caps block. Also needs
    # no game: what it checks is that slot N reaches method N and that an
    # unimplemented method REPORTS, which are host-side facts.
    add_test(NAME d3d8_host
             COMMAND x2native --no-window --d3d8-selftest)
    set_tests_properties(d3d8_host PROPERTIES SKIP_RETURN_CODE 77)
    # The engine's error dialog, which only ever runs when something has
    # already gone wrong -- so nothing else exercises it. Drives the override
    # with a synthetic call frame and checks the ABI and the screenless answer.
    add_test(NAME report_box
             COMMAND x2native --no-window --dialog-selftest)
    set_tests_properties(report_box PROPERTIES SKIP_RETURN_CODE 77)
    # The crash reporter, proved by crashing. Only SIGSEGV used to be handled,
    # so an illegal instruction or an arithmetic fault killed a run with
    # nothing printed at all -- a crash that read, from a log, exactly like the
    # window being closed. Each signal is raised in a child and the report must
    # name it; a control child that does not fault must stay silent.
    add_test(NAME fault_reporter
             COMMAND x2native --no-window --fault-selftest)
    set_tests_properties(fault_reporter PROPERTIES SKIP_RETURN_CODE 77)
else()
    message(STATUS
        "x2native: SKIPPED -- no module has generated output. "
        "Run tools/recomp.py emit and tools/recomp.py native (see above).")
endif()
