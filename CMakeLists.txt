cmake_minimum_required(VERSION 3.16)
project(xmen2_sdl LANGUAGES C CXX)

# ONE build tree. `./run.sh` maintains build/native, and every live harness
# executes that exact binary. A second same-named target once let a full live
# PASS describe an hour-old executable (issue #118), so arbitrary in-tree
# build directories remain refused. `scratch/` is only transient run data.
file(RELATIVE_PATH _x2_build_rel "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
if(NOT _x2_build_rel MATCHES "^\\.\\./" AND NOT _x2_build_rel MATCHES "^build/")
    message(FATAL_ERROR
        "Refusing to configure into '${_x2_build_rel}'.\n"
        "This project has ONE build tree: build/native, maintained by "
        "./run.sh and executed by every live harness. A second in-source tree "
        "builds a same-named x2native that nothing runs (issue #118).\n"
        "  ./run.sh                                    provision, build, launch\n"
        "  cmake --build build/native -j$(nproc)\n"
        "  ctest --test-dir build/native --output-on-failure\n"
        "Use build/native, another path under build/, or an external BUILD path.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Shared port infrastructure: the desktop picker uses Lucent's safe, tested
# archive extraction rather than carrying a second ZIP implementation here.
include(FetchContent)
set(LUCENT_BUILD_ZIP ON)
FetchContent_Declare(lucent
    GIT_REPOSITORY https://github.com/SomeoneIsWorking/lucent.git
    GIT_TAG 30ef243
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(lucent)

# Unit executables compile shipping subsystem sources directly, so they must
# use the same config/logger owners as the product rather than gaining test
# replacements. This directory default is cleared before external shared
# projects are added below.
add_library(x2_runtime_services STATIC
    src/config/environment.c
    src/native/x2_log.c)
set_target_properties(x2_runtime_services PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(x2_runtime_services PUBLIC src/config src/native)
target_link_libraries(x2_runtime_services PUBLIC lucent::lucent)
link_libraries(x2_runtime_services)

add_library(x2_runtime_config STATIC
    src/config/config_directory.c
    src/config/runtime_cvars.cpp)
set_target_properties(x2_runtime_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(x2_runtime_config PUBLIC src/config)
target_link_libraries(x2_runtime_config PUBLIC x2_runtime_services
                                               lucent::lucent)

add_executable(test_x2_log tests/test_x2_log.cpp)
target_link_libraries(test_x2_log PRIVATE x2_runtime_services)
add_test(NAME x2_log COMMAND test_x2_log)
set(X2_LUCENT_ANDROID_JAVA_DIR
    "${lucent_SOURCE_DIR}/platforms/android/java")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

# SDL3, for its GPU abstraction: it is the layer that will stand in for D3D8,
# and it maps onto Metal on macOS rather than needing MoltenVK underneath.
# Android is deliberately a separate dependency boundary. Host pkg-config
# metadata can describe x86 desktop libraries while CMake is targeting ARM64;
# accepting it creates a build that configures successfully and cannot link or
# run on a phone.
if(ANDROID)
    if(NOT X2_ANDROID_PORT_PREFIX)
        message(FATAL_ERROR
            "x2native: Android requires the shared android-port native prefix. "
            "Run tools/build_android.py without --skip-deps, then configure with "
            "-DX2_ANDROID_PORT_PREFIX=<prefix>.")
    endif()
    set(_x2_android_manifest
        "${X2_ANDROID_PORT_PREFIX}/android-port-dependencies.json")
    if(NOT EXISTS "${_x2_android_manifest}")
        message(FATAL_ERROR
            "x2native: Android dependency manifest is missing: ${_x2_android_manifest}. "
            "Rebuild the shared android-port prefix.")
    endif()
    file(READ "${_x2_android_manifest}" _x2_android_manifest_text)
    string(REGEX MATCH "\"abi\"[ \t\r\n]*:[ \t\r\n]*\"${ANDROID_ABI}\""
           _x2_android_abi_match "${_x2_android_manifest_text}")
    if(NOT _x2_android_abi_match)
        message(FATAL_ERROR
            "x2native: ${_x2_android_manifest} does not describe ABI ${ANDROID_ABI}.")
    endif()
    string(REGEX MATCH "\"platform\"[ \t\r\n]*:[ \t\r\n]*\"${ANDROID_PLATFORM}\""
           _x2_android_platform_match "${_x2_android_manifest_text}")
    if(NOT _x2_android_platform_match)
        message(FATAL_ERROR
            "x2native: ${_x2_android_manifest} does not describe platform ${ANDROID_PLATFORM}.")
    endif()
    foreach(_x2_ffmpeg_library IN ITEMS avformat avcodec swscale swresample avutil)
        if(NOT EXISTS "${X2_ANDROID_PORT_PREFIX}/lib/lib${_x2_ffmpeg_library}.a")
            message(FATAL_ERROR
                "x2native: Android FFmpeg is incomplete: missing "
                "${X2_ANDROID_PORT_PREFIX}/lib/lib${_x2_ffmpeg_library}.a. "
                "Rebuild the shared android-port prefix.")
        endif()
    endforeach()
    add_library(x2_android_ffmpeg INTERFACE)
    target_include_directories(x2_android_ffmpeg INTERFACE
                               "${X2_ANDROID_PORT_PREFIX}/include")
    target_link_libraries(x2_android_ffmpeg INTERFACE
        "${X2_ANDROID_PORT_PREFIX}/lib/libavformat.a"
        "${X2_ANDROID_PORT_PREFIX}/lib/libavcodec.a"
        "${X2_ANDROID_PORT_PREFIX}/lib/libswscale.a"
        "${X2_ANDROID_PORT_PREFIX}/lib/libswresample.a"
        "${X2_ANDROID_PORT_PREFIX}/lib/libavutil.a"
        log android dl z m)
    set(X2_FFMPEG_TARGET x2_android_ffmpeg)

    find_package(SDL3 CONFIG REQUIRED
                 PATHS "${X2_ANDROID_PORT_PREFIX}/lib/cmake/SDL3"
                 NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT TARGET SDL3::SDL3-shared)
        message(FATAL_ERROR "x2native: shared android-port prefix omitted SDL3::SDL3-shared")
    endif()
    set(X2_SDL SDL3::SDL3-shared)
    set(X2_SDL_ANDROID_JAVA_DIR
        "${X2_ANDROID_PORT_PREFIX}/share/android-port/sdl3-java")
    set(X2_ANDROID_CXX_LIBRARY
        "${X2_ANDROID_PORT_PREFIX}/share/android-port/cxx/${ANDROID_ABI}/libc++_shared.so")
    if(NOT EXISTS "${X2_ANDROID_CXX_LIBRARY}")
        message(FATAL_ERROR
            "x2native: shared android-port prefix omitted ${X2_ANDROID_CXX_LIBRARY}.")
    endif()

    find_package(SDL3_image CONFIG REQUIRED
                 PATHS "${X2_ANDROID_PORT_PREFIX}/lib/cmake/SDL3_image"
                 NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(TARGET SDL3_image::SDL3_image-static)
        set(X2_SDL_IMAGE SDL3_image::SDL3_image-static)
    elseif(TARGET SDL3_image::SDL3_image)
        set(X2_SDL_IMAGE SDL3_image::SDL3_image)
    else()
        message(FATAL_ERROR "x2native: shared android-port prefix omitted SDL3_image target")
    endif()

    # RmlUi needs a font engine to initialise. The shared Android prefix owns
    # the matching cross-compiled FreeType capability; resolving it here keeps
    # the title from ever falling back to a host package.
    find_package(Freetype CONFIG REQUIRED
                 PATHS "${X2_ANDROID_PORT_PREFIX}/lib/cmake/freetype"
                 NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT TARGET Freetype::Freetype)
        message(FATAL_ERROR
            "x2native: shared android-port prefix omitted Freetype::Freetype")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X2_FFMPEG REQUIRED IMPORTED_TARGET
                      libavformat libavcodec libavutil libswscale libswresample)
    set(X2_FFMPEG_TARGET PkgConfig::X2_FFMPEG)
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

add_executable(test_assertions_enabled tests/test_assertions_enabled.c)
add_test(NAME assertions_enabled COMMAND test_assertions_enabled)

add_executable(test_config_directory tests/test_config_directory.c
               src/config/config_directory.c)
target_include_directories(test_config_directory PRIVATE src/config)
target_link_libraries(test_config_directory PRIVATE lucent::lucent)
add_test(NAME config_directory COMMAND test_config_directory)

add_executable(test_install_picker tests/test_install_picker.c
               src/native/install_picker.cpp src/native/install_archive.cpp
               src/native/install_validation.cpp
               src/config/config_directory.c)
target_include_directories(test_install_picker PRIVATE src/native src/config
                           ${CMAKE_BINARY_DIR})
target_link_libraries(test_install_picker PRIVATE ${X2_SDL} lucent::lucent lucent::zip)
add_test(NAME install_picker COMMAND test_install_picker)

add_executable(test_install_archive tests/test_install_archive.cpp
               src/native/install_archive.cpp src/native/install_validation.cpp
               src/config/config_directory.c)
target_include_directories(test_install_archive PRIVATE src/native src/config
                            ${CMAKE_BINARY_DIR})
target_link_libraries(test_install_archive PRIVATE lucent::lucent lucent::zip)
add_test(NAME install_archive COMMAND test_install_archive)

add_executable(test_touch_controls tests/test_touch_controls.cpp
               src/input/touch_controls.cpp
               src/presentation/touch_layout.c)
target_include_directories(test_touch_controls PRIVATE src/input)
target_link_libraries(test_touch_controls PRIVATE lucent::lucent)
add_test(NAME touch_controls COMMAND test_touch_controls)

add_executable(test_gameplay_control tests/test_gameplay_control.c
               src/input/gameplay_control.c)
add_test(NAME gameplay_control COMMAND test_gameplay_control)

add_executable(test_touch_layout tests/test_touch_layout.c
               src/presentation/touch_layout.c)
add_test(NAME touch_layout COMMAND test_touch_layout)

add_executable(test_gpu_frame_timing tests/test_gpu_frame_timing.c
               src/gpu/gpu_frame_timing.c)
target_include_directories(test_gpu_frame_timing PRIVATE src/gpu)
add_test(NAME gpu_frame_timing COMMAND test_gpu_frame_timing)

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

add_executable(test_gpu_texture_format tests/test_gpu_texture_format.c
    src/gpu/gpu_texture_format.c)
target_include_directories(test_gpu_texture_format PRIVATE src/gpu)
add_test(NAME gpu_texture_format COMMAND test_gpu_texture_format)

add_executable(test_movie_audio tests/test_movie_audio.c
               src/audio/movie_audio.c)
target_include_directories(test_movie_audio PRIVATE src/audio)
target_link_libraries(test_movie_audio PRIVATE m pthread)
add_test(NAME movie_audio COMMAND test_movie_audio)

add_executable(test_cutscene_script_audio tests/test_cutscene_script_audio.c
               src/native/cutscene_script_audio.c src/native/guest_memory.c)
target_include_directories(test_cutscene_script_audio PRIVATE src/native src/runtime/x86_abi)
add_test(NAME cutscene_script_audio COMMAND test_cutscene_script_audio)

add_executable(test_behaved_context tests/test_behaved_context.c
               src/native/behaved_context.c src/native/guest_memory.c)
target_include_directories(test_behaved_context PRIVATE src/native src/runtime/x86_abi)
add_test(NAME behaved_context COMMAND test_behaved_context)

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
    target_link_libraries(test_fmv_decode PRIVATE ${X2_FFMPEG_TARGET} m)
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
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     SKIP_RETURN_CODE 77)
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
target_include_directories(test_boot_menu_transition PRIVATE src/native src/runtime/x86_abi)
add_test(NAME boot_menu_transition COMMAND test_boot_menu_transition)

add_executable(test_x86_guest_call_stack tests/test_x86_guest_call_stack.c
               src/native/x86_guest_call_stack.c)
target_include_directories(test_x86_guest_call_stack PRIVATE src/native)
target_link_libraries(test_x86_guest_call_stack PRIVATE Threads::Threads)
add_test(NAME x86_guest_call_stack COMMAND test_x86_guest_call_stack)

add_executable(test_jit_intercept tests/test_jit_intercept.c
               src/native/x86_engine_intercept.c
               src/native/x86_guest_call_stack.c)
target_include_directories(test_jit_intercept PRIVATE src/native src/runtime/x86_abi)
target_link_libraries(test_jit_intercept PRIVATE x86port_runtime)
add_test(NAME jit_intercept COMMAND test_jit_intercept)

add_executable(test_joystick_neutral tests/test_joystick_neutral.c
               src/input/joystick_neutral.c)
target_include_directories(test_joystick_neutral PRIVATE src/input)
add_test(NAME joystick_neutral COMMAND test_joystick_neutral)

add_executable(test_aspect_fit tests/test_aspect_fit.c
               src/presentation/aspect_fit.c)
target_include_directories(test_aspect_fit PRIVATE src/presentation)
add_test(NAME aspect_fit COMMAND test_aspect_fit)

add_executable(test_win32_mouse tests/test_win32_mouse.c
               src/native/win32_mouse.c
               src/presentation/aspect_fit.c)
target_include_directories(test_win32_mouse PRIVATE src/native src/presentation)
add_test(NAME win32_mouse COMMAND test_win32_mouse)

add_executable(test_d3d8_live_resolution
               tests/test_d3d8_live_resolution.c
               src/d3d8/d3d8_live_resolution.c)
target_include_directories(test_d3d8_live_resolution PRIVATE src/d3d8 src/gpu)
add_test(NAME d3d8_live_resolution COMMAND test_d3d8_live_resolution)

add_executable(test_dialog_selection_scale_policy
               tests/test_dialog_selection_scale_policy.c
               src/native/dialog_selection_scale_policy.c)
target_include_directories(test_dialog_selection_scale_policy PRIVATE
                           src/native src/presentation)
target_link_libraries(test_dialog_selection_scale_policy PRIVATE m)
add_test(NAME dialog_selection_scale_policy
         COMMAND test_dialog_selection_scale_policy)

add_executable(test_d3d8_selector_probe
               tests/test_d3d8_selector_probe.c
               src/d3d8/d3d8_selector_probe.c
               src/d3d8/d3d8_selector_probe_json.c
               src/d3d8/d3d8_texture_provenance.c)
target_include_directories(test_d3d8_selector_probe PRIVATE src/d3d8 src/gpu)
target_link_libraries(test_d3d8_selector_probe PRIVATE m)
add_test(NAME d3d8_selector_probe COMMAND test_d3d8_selector_probe)
set(_x2_selector_probe_fixture
    "${CMAKE_CURRENT_BINARY_DIR}/selector-probe-selftest.jsonl")
add_test(NAME selector_probe_writer
         COMMAND ${CMAKE_COMMAND} -E env
                 X2_SELECTOR_PROBE=${_x2_selector_probe_fixture}
                 X2_SELECTOR_TEXTURE=128x32
                 $<TARGET_FILE:test_d3d8_selector_probe>)
add_test(NAME selector_probe_schema
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/selector_probe.py
                 ${_x2_selector_probe_fixture} --all)
set_tests_properties(selector_probe_schema PROPERTIES
                     DEPENDS selector_probe_writer)

add_executable(test_live_resolution tests/test_live_resolution.c
               src/presentation/live_resolution.c)
target_include_directories(test_live_resolution PRIVATE
                           src/presentation src/config src/d3d8 src/native)
add_test(NAME live_resolution COMMAND test_live_resolution)

add_executable(test_load_game_menu_policy
               tests/test_load_game_menu_policy.c
               src/save/load_game_menu_policy.c)
target_include_directories(test_load_game_menu_policy PRIVATE src/save)
add_test(NAME load_game_menu_policy COMMAND test_load_game_menu_policy)
add_executable(test_gpu_present_resize tests/test_gpu_present_resize.c
               src/gpu/gpu_present.c
               src/presentation/aspect_fit.c)
target_include_directories(test_gpu_present_resize PRIVATE src/gpu
                           src/presentation)
target_compile_definitions(test_gpu_present_resize PRIVATE
    SDL_CreateGPUTexture=x2_test_create_gpu_texture
    SDL_ReleaseGPUTexture=x2_test_release_gpu_texture)
target_link_libraries(test_gpu_present_resize PRIVATE ${X2_SDL})
add_test(NAME gpu_present_resize COMMAND test_gpu_present_resize)

add_test(NAME selector_probe_tool
         COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_selector_probe)
set_tests_properties(selector_probe_tool PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
target_include_directories(test_player_input PRIVATE src/runtime/x86_abi src/native
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
               src/input/input_record.c src/native/json_string.c
               src/config/directory_create.c)
target_include_directories(test_input_record PRIVATE src/input src/native
                           src/config)
add_test(NAME input_record COMMAND test_input_record)
set_tests_properties(input_record PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_dinput_system tests/test_dinput_system.c
               src/native/dinput_system.c tests/native_stubs.c)
target_include_directories(test_dinput_system PRIVATE src/runtime/x86_abi src/native)
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
                           src/native src/runtime/x86_abi)
target_compile_definitions(test_cutscene_dialogue PRIVATE X86_NATIVE)
target_link_libraries(test_cutscene_dialogue PRIVATE pthread)
add_test(NAME cutscene_dialogue COMMAND test_cutscene_dialogue)

add_executable(test_behaved_player_heap
               tests/test_behaved_player_heap.c
               src/native/behaved_player.c src/native/guest_memory.c)
target_include_directories(test_behaved_player_heap PRIVATE
                           src/native src/runtime/x86_abi)
target_compile_definitions(test_behaved_player_heap PRIVATE X86_NATIVE)
target_link_libraries(test_behaved_player_heap PRIVATE pthread)
add_test(NAME behaved_player_heap COMMAND test_behaved_player_heap)

add_executable(test_cutscene_event_player
               tests/test_cutscene_event_player.c
               src/native/cutscene_event_player.c src/native/guest_memory.c)
target_include_directories(test_cutscene_event_player PRIVATE
                           src/native src/runtime/x86_abi)
target_compile_definitions(test_cutscene_event_player PRIVATE X86_NATIVE)
target_link_libraries(test_cutscene_event_player PRIVATE pthread)
add_test(NAME cutscene_event_player COMMAND test_cutscene_event_player)

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
    "${CMAKE_SOURCE_DIR}/src/gen/prompt_glyph_atlas.h")
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
               src/native/dinput_pad_identity.c
        src/native/dinput_pad_virtual.c src/native/dinput_pad_virtual_actions.c
        src/native/guest_clock.c)
target_include_directories(test_dinput_pad PRIVATE src/native)
target_compile_definitions(test_dinput_pad PRIVATE X2_WITH_SDL)
target_link_libraries(test_dinput_pad PRIVATE ${X2_SDL})
add_test(NAME dinput_pad COMMAND test_dinput_pad)

add_executable(test_directinput_controller_sample
    tests/test_directinput_controller_sample.c
    src/input/directinput_controller_sample.c)
target_include_directories(test_directinput_controller_sample
    PRIVATE src/input src/native)
target_compile_options(test_directinput_controller_sample PRIVATE -UNDEBUG)
add_test(NAME directinput_controller_sample
         COMMAND test_directinput_controller_sample)

add_executable(test_pad_glyphs tests/test_pad_glyphs.c
               src/native/pad_glyphs.c src/native/prompt_glyphs.c
               src/native/prompt_labels.c src/native/input_binding_sets.c
               src/native/input_bindings.c
               src/native/guest_memory.c
               tests/native_stubs.c)
add_dependencies(test_pad_glyphs x2_pad_glyph_header)
target_include_directories(test_pad_glyphs PRIVATE src/runtime/x86_abi src/native
                                                  src/input ${CMAKE_BINARY_DIR})
target_compile_definitions(test_pad_glyphs PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_pad_glyphs PRIVATE pthread x2_runtime_config)
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
                           PRIVATE src/runtime/x86_abi src/native src/gen
                                   ${CMAKE_BINARY_DIR})
target_compile_definitions(test_prompt_glyph_draw PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_prompt_glyph_draw PRIVATE m pthread x2_runtime_config)
add_test(NAME prompt_glyph_draw COMMAND test_prompt_glyph_draw)

add_executable(test_prompt_glyph_metrics tests/test_prompt_glyph_metrics.c
               src/native/prompt_glyph_metrics.c
               src/native/prompt_glyphs.c
               src/native/prompt_glyph_atlas.c src/native/guest_memory.c
               tests/native_stubs.c)
add_dependencies(test_prompt_glyph_metrics x2_pad_glyph_header
                 x2_prompt_glyph_atlas)
target_include_directories(test_prompt_glyph_metrics
                           PRIVATE src/runtime/x86_abi src/native src/gen
                                   ${CMAKE_BINARY_DIR})
target_compile_definitions(test_prompt_glyph_metrics PRIVATE _GNU_SOURCE
                                                           X86_NATIVE)
target_link_libraries(test_prompt_glyph_metrics PRIVATE m pthread)
add_test(NAME prompt_glyph_metrics COMMAND test_prompt_glyph_metrics)

add_executable(test_prompt_glyph_batch tests/test_prompt_glyph_batch.c
               src/native/prompt_glyph_batch.c tests/native_stubs.c)
target_include_directories(test_prompt_glyph_batch
                           PRIVATE src/runtime/x86_abi src/native src/gpu)
add_test(NAME prompt_glyph_batch COMMAND test_prompt_glyph_batch)

add_executable(test_ui_transform tests/test_ui_transform.c
               src/native/ui_transform.c src/gpu/gpu_matrix.c
               src/native/guest_memory.c tests/native_stubs.c)
target_include_directories(test_ui_transform
                           PRIVATE src/runtime/x86_abi src/native src/gpu)
target_compile_definitions(test_ui_transform PRIVATE X86_NATIVE)
target_link_libraries(test_ui_transform PRIVATE pthread)
add_test(NAME ui_transform COMMAND test_ui_transform)

add_executable(test_dialog_prompts tests/test_dialog_prompts.c
               src/native/dialog_prompts.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_dialog_prompts PRIVATE src/runtime/x86_abi src/native
                                                       src/input)
target_compile_definitions(test_dialog_prompts PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_dialog_prompts PRIVATE pthread)
add_test(NAME dialog_prompts COMMAND test_dialog_prompts)

add_executable(test_xbox_defaults tests/test_xbox_defaults.c
               src/native/xbox_defaults.c)
target_include_directories(test_xbox_defaults PRIVATE src/native)
add_test(NAME xbox_defaults COMMAND test_xbox_defaults)


# The guest printf format walker. It builds crt.c against a stub runtime, so it
# needs no guest runtime and runs in the ordinary suite.
add_executable(test_vformat tests/test_vformat.c src/native/crt.c
               src/native/crt_write_watch.c src/runtime/x86_abi/x87crt.c
            tests/native_stubs.c
               src/native/guest_heap.c src/native/guest_memory.c)
target_include_directories(test_vformat PRIVATE src/runtime/x86_abi src/native)
target_compile_definitions(test_vformat PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_vformat PRIVATE m pthread)
add_test(NAME vformat COMMAND test_vformat)

add_executable(test_x87crt tests/test_x87crt.c src/runtime/x86_abi/x87crt.c)

add_executable(test_crt_in_image_overrides tests/test_crt_in_image_overrides.c
               src/native/crt_in_image_overrides.c src/runtime/x86_abi/x87crt.c
               tests/native_stubs.c)
target_include_directories(test_crt_in_image_overrides PRIVATE src/native src/runtime/x86_abi)
target_link_libraries(test_crt_in_image_overrides PRIVATE m)
add_test(NAME crt_in_image_overrides COMMAND test_crt_in_image_overrides)

add_executable(test_audio_adpcm tests/test_audio_adpcm.c
               src/native/audio_adpcm.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_audio_adpcm PRIVATE src/native src/runtime/x86_abi)
add_test(NAME audio_adpcm COMMAND test_audio_adpcm)

add_executable(test_vertex_color_swizzle tests/test_vertex_color_swizzle.c
               src/native/vertex_color_swizzle.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_vertex_color_swizzle PRIVATE src/native src/runtime/x86_abi)
add_test(NAME vertex_color_swizzle COMMAND test_vertex_color_swizzle)

add_executable(test_vertex_builder tests/test_vertex_builder.c
               src/native/vertex_builder.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_vertex_builder PRIVATE src/native src/runtime/x86_abi)
add_test(NAME vertex_builder COMMAND test_vertex_builder)

add_executable(test_x86_import_fastpath tests/test_x86_import_fastpath.c
               src/native/x86_import_fastpath.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_x86_import_fastpath PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_x86_import_fastpath PRIVATE TEST_SUITE=1)
target_link_libraries(test_x86_import_fastpath PRIVATE x86port_runtime lucent::lucent)
add_test(NAME x86_import_fastpath COMMAND test_x86_import_fastpath)

add_executable(test_attr_stack tests/test_attr_stack.c
               src/native/attr_stack.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_attr_stack PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_attr_stack PRIVATE TEST_SUITE=1)
target_link_libraries(test_attr_stack PRIVATE lucent::lucent)
add_test(NAME attr_stack COMMAND test_attr_stack)

add_executable(test_audio_channel_poll tests/test_audio_channel_poll.c
               src/native/audio_channel_poll.c src/native/guest_memory.c
               tests/native_stubs.c)
target_include_directories(test_audio_channel_poll PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_audio_channel_poll PRIVATE TEST_SUITE=1)
target_link_libraries(test_audio_channel_poll PRIVATE lucent::lucent)
add_test(NAME audio_channel_poll COMMAND test_audio_channel_poll)

target_include_directories(test_x87crt PRIVATE src/runtime/x86_abi)
target_link_libraries(test_x87crt PRIVATE m)
add_test(NAME x87crt COMMAND test_x87crt)

# This is deliberately a hardware-x87 oracle: its inline assembly proves the
# ordinary-host-call bridge drains the physical x87 stack. AArch64 has no such
# stack; its translated calls use the model in x86rt.h and are covered by the
# architecture-independent x87 tests above.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i.86)$")
    add_executable(test_x87host tests/test_x87host.c src/runtime/x86_abi/x87host.c)
    target_include_directories(test_x87host PRIVATE src/runtime/x86_abi)
    target_link_libraries(test_x87host PRIVATE m)
    add_test(NAME x87host COMMAND test_x87host)
endif()

add_executable(test_x86callbacks
    tests/test_x86callbacks.c src/runtime/x86_abi/x86callbacks.c
    src/native/guest_memory.c tests/native_stubs.c)
target_include_directories(test_x86callbacks PRIVATE src/runtime/x86_abi src/native)
target_compile_definitions(test_x86callbacks PRIVATE X86_NATIVE)
target_link_libraries(test_x86callbacks PRIVATE pthread)
add_test(NAME x86callbacks COMMAND test_x86callbacks)

# The native executable, not only run.sh, loads the project .env. These tests
# link the exact shipping parser and prove both precedence directions plus its
# refusal of malformed input and executable-project precedence.
add_executable(test_env_file tests/test_env_file.c src/native/env_file.c)
target_include_directories(test_env_file PRIVATE src/native src/config)
add_test(NAME env_file_load COMMAND test_env_file load)
add_test(NAME env_file_preserve COMMAND test_env_file preserve)
add_test(NAME env_file_malformed COMMAND test_env_file malformed)
add_test(NAME env_file_unknown_key COMMAND test_env_file unknown-key)
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
target_include_directories(test_options_menu PRIVATE src/runtime/x86_abi src/native src/ui)
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
add_test(NAME build_shim COMMAND ${Python3_EXECUTABLE} -m unittest
         tests.test_build_shim)
set_tests_properties(build_shim PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME run_shim COMMAND ${Python3_EXECUTABLE} -m unittest
         tests.test_run_shim)
set_tests_properties(run_shim PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})



# The host D3D8 interface tables, against a real d3d8.h and against the game's
# own call sites. Runs the checker's SELF-TEST, not the check: the check needs
# scratch/analysis/libIGGfx.json, which is diagnostic input and gitignored, while the
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

add_test(NAME appimage_packaging
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/package_appimage.py --selftest)

add_test(NAME appimage_env
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_appimage_env.py
                 $<TARGET_FILE:x2native>)
set_tests_properties(appimage_env PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME android_release
         COMMAND ${CMAKE_COMMAND} -E env
                 PYTHONPATH=${CMAKE_SOURCE_DIR} ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_android_release.py)
set_tests_properties(android_release PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME android_qualify
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_android_qualify.py)
set_tests_properties(android_qualify PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME port_pause_menu
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/make_port_pause_menu.py --selftest)

add_test(NAME launcher
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher.py)
set_tests_properties(launcher PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


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

add_test(NAME port_tool_migrations
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_port_tool_migrations.py)
set_tests_properties(port_tool_migrations PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}")

add_test(NAME runtime_boundary_source
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/runtime_boundary.py --source)
set_tests_properties(runtime_boundary_source PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME source_policy
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/verify_source_policy.py)
set_tests_properties(source_policy PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME source_policy_selftest
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/verify_source_policy.py --selftest)
set_tests_properties(source_policy_selftest PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME clang_tidy
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/clang_tidy.py
                 --build-dir ${CMAKE_BINARY_DIR} --jobs 2)
set_tests_properties(clang_tidy PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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

# RelWithDebInfo defines NDEBUG, but the C test programs use assert() both for
# checks and, in older tests, for the expressions that exercise production
# code.  A test target must never inherit the shipping assertion policy: doing
# so made CTest report PASS without evaluating those expressions (issue #134).
get_property(_x2_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_x2_target IN LISTS _x2_directory_targets)
    if(_x2_target MATCHES "^test_")
        if(MSVC)
            target_compile_options(${_x2_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${_x2_target} PRIVATE -UNDEBUG)
        endif()
    endif()
endforeach()


# ---------------------------------------------------------------------------
# x2native -- native host services plus runtime-translated guest code.
# The required images remain user-supplied data. No source generator, emitted
# guest corpus, static dispatcher, or execution-engine selector participates in
# this target.
set(X2_MODULES libIGDisplay libIGCore libIGSg libIGMath libIGAttrs
               libIGGfx libIGUtils libIGGui libIGLua libIGOpt
               libMovie libCriMovie libIGAudio libIGCollision
               libIGInsight libIGViewer
               cg cgD3D8 msdia80 XMen2)
set(X2_INSTALL_REQUIRED_IMAGES "")
foreach(m ${X2_MODULES})
    if(m STREQUAL "XMen2")
        list(APPEND X2_INSTALL_REQUIRED_IMAGES "${m}.exe")
    else()
        list(APPEND X2_INSTALL_REQUIRED_IMAGES "${m}.dll")
    endif()
endforeach()
list(JOIN X2_INSTALL_REQUIRED_IMAGES "\",\n    \""
     X2_INSTALL_REQUIRED_IMAGES_LITERAL)
# A selected executable and its loader DLLs are not a playable PC install.
# These title-owned assets cover every boot-time content family, so the
# desktop and Android setup boundaries reject a copied loader-only directory
# before it becomes the persisted selection.
set(X2_INSTALL_REQUIRED_CONTENT
    build.ini
    xmen.ini
    Actors/0000.IGB
    Conversations/1_deadzone2_0040.XMLB
    Data/common_ents.XMLB
    Dialogs/standard_ok.XMLB
    HUD/hud_head_0000.IGB
    Maps/Act0/tutorial/tutorial1.IGB
    Movies/cine01.XMLB
    Packages/generated/characters/abyss_0101.PKGB
    Scripts/act0/tutorial/tutorial1/1stspscr_set_invisible_f.PY
    Sounds/badaudio.wav
    Textures/alphagrit.IGB
    UI/fonts/fonts.XMLB)
list(JOIN X2_INSTALL_REQUIRED_CONTENT "\",\n    \""
     X2_INSTALL_REQUIRED_CONTENT_LITERAL)
configure_file(src/native/install_requirements.h.in
               ${CMAKE_BINARY_DIR}/install_requirements.h @ONLY)
# Pin the exact RmlUi revision and its maintained SDL3/SDL_GPU backend. Pin
# both the commit and archive digest: a UI dependency update
# is a reviewed source change, never whatever GitHub serves that day.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
# Every packaged UI needs RmlUi's font engine at runtime. Android resolves
# the same target from the shared cross-compiled dependency prefix above.
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
# RmlUi asks CMake for Freetype again. Prefer the already resolved config
# package so its generic finder does not probe host paths or print a false
# "library missing" warning for the valid Android imported target.
if(DEFINED CMAKE_FIND_PACKAGE_PREFER_CONFIG)
    set(_x2_previous_find_package_preference
        "${CMAKE_FIND_PACKAGE_PREFER_CONFIG}")
    set(_x2_had_find_package_preference TRUE)
endif()
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
FetchContent_Declare(rmlui
    URL https://github.com/mikke89/RmlUi/archive/f9b8c9e2935d5df2c7dff2c190d3968e99b0c3dc.tar.gz
    URL_HASH SHA256=42b830abc2509a8c07cf02ba3313505de3a4642725654f489402022d8fefdd79
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(rmlui)
if(_x2_had_find_package_preference)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG
        "${_x2_previous_find_package_preference}")
else()
    unset(CMAKE_FIND_PACKAGE_PREFER_CONFIG)
endif()
unset(_x2_previous_find_package_preference)
unset(_x2_had_find_package_preference)
if(NOT ANDROID)
    find_package(SDL3_image REQUIRED)
    set(X2_SDL_IMAGE SDL3_image::SDL3_image)
endif()
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
                                            ${X2_SDL_IMAGE})
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ui")
configure_file(
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
    "${CMAKE_BINARY_DIR}/ui/LatoLatin-Regular.ttf" COPYONLY)
configure_file(
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf"
    "${CMAKE_BINARY_DIR}/ui/LatoLatin-Bold.ttf" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/assets/ui/settings.rcss"
               "${CMAKE_BINARY_DIR}/ui/settings.rcss" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/assets/ui/touch_controls.rcss"
               "${CMAKE_BINARY_DIR}/ui/touch_controls.rcss" COPYONLY)
set(X2_TOUCH_ICON_STAMP "${CMAKE_BINARY_DIR}/ui/touch/.touch-icons.stamp")
add_custom_command(
    OUTPUT "${X2_TOUCH_ICON_STAMP}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            --copy-svg-dir "${CMAKE_BINARY_DIR}/ui/touch"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            "${CMAKE_SOURCE_DIR}/tools/shared_dir.py"
            "${CMAKE_SOURCE_DIR}/assets/buttons/glyphs.json"
            ${X2_PROMPT_GLYPH_INPUTS}
    COMMENT "x2native: staging shared SVGs for touch-control feedback"
    VERBATIM)
add_custom_target(x2_touch_icons DEPENDS "${X2_TOUCH_ICON_STAMP}")
add_library(x2_rmlui_ui STATIC
    src/ui/controller_assignment_rows.cpp
    src/ui/rmlui_ui.cpp
    src/ui/ui_resources.cpp
    src/ui/settings_document.cpp
    src/ui/touch_document.cpp
    src/ui/settings_overlay_state.c)
add_dependencies(x2_rmlui_ui x2_touch_icons)
target_include_directories(x2_rmlui_ui PRIVATE src/native src/config
                                               src/presentation src/input
                                               src/gpu)
target_compile_definitions(x2_rmlui_ui PRIVATE
    X2_UI_FONT_PATH="${CMAKE_BINARY_DIR}/ui/LatoLatin-Regular.ttf"
    X2_UI_FONT_BOLD_PATH="${CMAKE_BINARY_DIR}/ui/LatoLatin-Bold.ttf"
    X2_UI_RESOURCE_DIR="${CMAKE_BINARY_DIR}/ui"
    X2_UI_DOCUMENT_URL="${CMAKE_BINARY_DIR}/ui/settings.rml")
set_target_properties(x2_rmlui_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(x2_rmlui_ui PUBLIC x2_rmlui_backend
                                        PRIVATE lucent::lucent)

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

set(X2_NATIVE_SOURCES
    src/native/x2native.c
    src/native/fault_report.c
    src/native/engine_file_path.c
    src/native/guest_memory.c
    src/native/x2native_options.c
    src/native/install_picker.cpp
    src/native/install_archive.cpp
    src/native/install_validation.cpp
    src/native/android_bridge.cpp
    src/input/alchemy_controller_bridge.cpp
    src/input/directinput_controller_sample.c
    src/input/ig_controller_manager_adapter.cpp
    src/config/directory_create.c
    src/config/settings.c
    src/config/input_assignments.c
    src/config/settings_store.c
    src/config/boot_mode.c
    src/native/boot_mode_policy.c
    src/native/boot_mode_runtime.c
    src/native/boot_menu_transition.c
    src/native/boot_splash_policy.c
    src/native/x86_engine.c
    src/native/guest_modules.c
    src/native/host_imports_advapi32.c
    src/native/host_imports_all.c
    src/native/host_imports_dinput.c
    src/native/host_imports_dinput8.c
    src/native/host_imports_d3d8.c
    src/native/host_imports_gdi32.c
    src/native/host_imports_kernel32.c
    src/native/host_imports_msvcr71.c
    src/native/host_imports_msvcrt.c
    src/native/host_imports_ole32.c
    src/native/host_imports_user32.c
    src/native/host_imports_winmm.c
    src/native/host_imports_ws2_32.c
    src/native/host_imports.c
    src/native/guest_body.c
    src/native/x86_engine_selftest.c
    src/native/x86_dispatch.c
    src/native/x86_diag_dump.c
    src/native/x86_guest_call_stack.c
    src/native/x86_engine_intercept.c
    src/native/x86_engine_dispatch.c
    src/native/x86_import_fastpath.c
    src/native/attr_stack.c
    src/native/attr_stack_verify.c
    src/native/x86_engine_jit_diag.c
    src/native/x86_dispatch_report.c
    src/presentation/aspect_fit.c
    src/presentation/boot_blackout.c
    src/presentation/window_settings.c
    src/presentation/display_mode_seed.c
    src/presentation/live_resolution.c
    src/native/display_mode_runtime.c
    src/native/ui_text_scale.c
    src/native/dialog_selection_scale.c
    src/native/dialog_selection_scale_policy.c
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
    src/save/load_game_menu_policy.c
    src/native/autosave_runtime.c
    src/native/boot_player_selection.c
    src/native/continue_runtime.c
    src/native/exact_save_load.c
    src/native/load_game_menu_runtime.c
    src/native/save_trace_runtime.c
    src/native/pe_map.c
    src/native/win32_sdl.c
    src/native/win32_mouse.c
    src/native/win32_pointer.c
    src/native/win32_events.c
    src/native/guest_heap.c
    src/native/crt.c
    src/native/crt_aliases.c
    src/native/crt_setjmp.c
    src/native/crt_write_watch.c
    src/native/crt_selftest.c
    src/native/crt_in_image_overrides.c
    src/native/audio_adpcm.c
    src/native/audio_adpcm_verify.c
    src/native/vertex_color_swizzle.c
    src/native/vertex_color_swizzle_verify.c
    src/native/vertex_builder.c
    src/native/vertex_builder_verify.c
    src/native/audio_channel_poll.c
    src/native/audio_channel_poll_verify.c
    src/runtime/x86_abi/x87crt.c
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
    src/native/dinput_pad_identity.c
    src/native/dinput_pad_virtual.c
    src/native/dinput_pad_virtual_actions.c
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
    src/native/behaved_context.c
    src/native/behaved_player.c
    src/native/cutscene_event_player.c
    src/native/conversation_player.c
    src/native/cutscene_dialogue.c
    src/native/cutscene_script_audio.c
    src/native/cutscene_player_policy.c
    src/native/cutscene_control_clock.c
    src/native/cutscene_player.c
    src/native/input_probe.c
    src/input/touch_controls.cpp
    src/input/touch_runtime.cpp
    src/presentation/touch_layout.c
    src/input/gameplay_control.c
    src/native/touch_hud_runtime.c
    src/native/xbox_defaults.c
    src/native/dsound.c
    src/audio/movie_audio.c
    src/media/fmv_audio_decode.c
    src/media/fmv_decoder_drain.c
    src/media/fmv_player.c
    src/media/fmv_policy.c
    src/media/fmv_probe.c
    src/media/fmv_timing.c
    src/native/guest_clock.c
    src/native/control.c
    src/native/control_http.c
    src/native/control_save_route.c
    src/native/control_performance_route.c
    src/native/control_status_route.c
    src/native/control_png.c
    src/native/control_query.c
    src/native/control_screenshot.c
    src/native/control_status.c
    src/native/json_string.c
    src/native/live_session.c
    src/native/ws2_32.c
    src/native/shell32.c
    src/native/winmm.c
    src/native/threads.c
    src/native/threads_engine_report.c
    src/native/x86_hotep.c
    src/native/run_reports.c
    src/native/x86rt_native.c
    src/native/env_file.c
    src/native/startup.c
    src/native/movie.c
    src/native/movie_image_layout.c
    src/native/options_menu.c
    src/native/reportbox.c
    src/native/security.c
    src/native/conversation.c
    src/native/script_trace.c
    src/native/entity_spawn_probe.c
    src/native/heartbeat.c
    src/d3d8/d3d8_com.c
    src/d3d8/d3d8_caps.c
    src/d3d8/d3d8_state.c
    src/d3d8/d3d8_stateblock.c
    src/d3d8/d3d8_d3d8.c
    src/d3d8/d3d8_device.c
    src/d3d8/d3d8_device_lights.c
    src/d3d8/d3d8_live_resolution.c
    src/d3d8/d3d8_selector_probe.c
    src/d3d8/d3d8_selector_probe_json.c
    src/d3d8/d3d8_selector_probe_bridge.c
    src/d3d8/d3d8_texture_provenance.c
    src/d3d8/d3d8_lightlog.c
    src/d3d8/d3d8_surface.c
    src/d3d8/d3d8_resource.c
    src/d3d8/d3d8_texture_luma.c
    src/d3d8/d3d8_texture_stage.c
    src/d3d8/d3d8_vertex_layout.c
    src/d3d8/d3d8_frame_material.c
    src/d3d8/d3d8_drawcall.c
    src/d3d8/d3d8_lighting.c
    src/d3d8/d3d8_light_dump.c
    src/d3d8/d3d8_light_survey.c
    src/d3d8/d3d8_vertex_shader.c
    src/d3d8/d3d8_vs_execute.cpp
    src/d3d8/d3d8_screen_space_test.c
    src/d3d8/d3d8_light_selftest.c
    src/d3d8/d3d8_host_report.c
    src/d3d8/d3d8_report.c
    src/vulkan/igvk_ark.c
    src/vulkan/igvk_probe.c
    src/vulkan/igvk_context.c
    src/gpu/gpu_device.c
    src/gpu/gpu_capture.c
    src/gpu/gpu_draw.c
    src/gpu/gpu_pipeline.c
    src/gpu/gpu_texture_format.c
    src/gpu/gpu_draw_trace.c
    src/gpu/gpu_frame_timing.c
    src/gpu/gpu_frame_timing_report.c
    src/gpu/gpu_shadow.c
    src/gpu/gpu_shadow_selftest.c
    src/gpu/shadow_policy.c
    src/gpu/gpu_frame_submit.c
    src/gpu/gpu_present.c
    src/gpu/gpu_present_selftest.c
    src/gpu/gpu_prompt_glyphs.c
    src/gpu/gpu_host_selftest.c
    src/gpu/gpu_multistage_selftest.c
    src/gpu/gpu_matrix.c
    src/gpu/gpu_frame_init_selftest.c
    src/gpu/gpu_upload.c
    src/gpu/gpu_upload_selftest.c
    src/gpu/gpu_headless.c
    src/gpu/gpu_device_selftest.c
    src/gpu/gpu_selftest.c
    src/gpu/gpu_texture_selftest.c
    src/vulkan/igvk_slots_lifecycle.c
    src/vulkan/igvk_slots_frame.c
)
if(ANDROID)
    add_library(x2native SHARED ${X2_NATIVE_SOURCES})
else()
    add_executable(x2native ${X2_NATIVE_SOURCES})
endif()
# -----------------------------------------------------------------
# The product-only runtime execution engine.
#
# CONSUMED, never vendored: `shared/x86port` owns x86-32 semantics for
# every title over it, and a copy in this tree that silently won is the
# exact failure that split exists to end. It is resolved by the same
# tools/shared_dir.py every other shared repo goes through, so there is one
# answer to "where is it" and one refusal naming every path tried.
#
# x86port consumes the shared code-cache library beside its checkout. Pass
# that path explicitly so an alternate x86port checkout cannot silently bind
# a different revision.
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
            "import sys; sys.path.insert(0, sys.argv[1]); import shared_dir; print(shared_dir.shared_dir(sys.argv[2], sys.argv[3]))"
            "${CMAKE_SOURCE_DIR}/tools" x86port src/x86port/jit_engine.h
    OUTPUT_VARIABLE X2_X86PORT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE X2_X86PORT_WHY RESULT_VARIABLE X2_X86PORT_RC)
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
            "import sys; sys.path.insert(0, sys.argv[1]); import shared_dir; print(shared_dir.shared_dir(sys.argv[2], sys.argv[3]))"
            "${CMAKE_SOURCE_DIR}/tools" jit-common src/jitcommon/block_cache.h
    OUTPUT_VARIABLE X2_JITCOMMON_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE X2_JITCOMMON_WHY RESULT_VARIABLE X2_JITCOMMON_RC)
if(X2_X86PORT_RC OR X2_JITCOMMON_RC)
    # REFUSED, not skipped. An engine that is absent and an engine that is
    # present but never selected produce identical runs, and telling those
    # apart is the whole point of the migration's measurement.
    message(FATAL_ERROR
        "x2native: the execution engine could not be resolved.\n"
        "${X2_X86PORT_WHY}${X2_JITCOMMON_WHY}"
        "Run ./run.sh to provision it, or set SHARED_DIR to a checkout root.")
endif()

# The neutral Alchemy layer has no CPU-runtime dependency. X-Men 2 consumes
# only the SDL-free controller owner here; exact guest layout and DirectInput
# conformance remain in this title's src/input adapter.
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
            "import sys; sys.path.insert(0, sys.argv[1]); import shared_dir; print(shared_dir.shared_dir(sys.argv[2], sys.argv[3]))"
            "${CMAKE_SOURCE_DIR}/tools" alchemy include/alchemy/input/controller.hpp
    OUTPUT_VARIABLE X2_ALCHEMY_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE X2_ALCHEMY_WHY RESULT_VARIABLE X2_ALCHEMY_RC)
if(X2_ALCHEMY_RC)
    message(FATAL_ERROR
        "x2native: the shared Alchemy runtime could not be resolved.\n"
        "${X2_ALCHEMY_WHY}"
        "Run ./run.sh to provision it, or set SHARED_DIR to a checkout root.")
endif()
set_property(DIRECTORY PROPERTY LINK_LIBRARIES "")
add_subdirectory("${X2_ALCHEMY_DIR}" "${CMAKE_BINARY_DIR}/alchemy"
                 EXCLUDE_FROM_ALL)
if(NOT TARGET alchemy::input)
    message(FATAL_ERROR
        "x2native: shared/alchemy does not provide alchemy::input; update the "
        "single pinned Alchemy checkout rather than adding a title-local copy.")
endif()
set(X86PORT_JITCOMMON_DIR "${X2_JITCOMMON_DIR}" CACHE PATH
    "jit-common checkout x86port builds against" FORCE)
# x2native links as a PIE (and on Android as a shared object), so every
# static library it pulls in has to be position-independent. Set here
# rather than in x86port, because it is this consumer's link mode that
# requires it -- x86port's own build has no reason to care, and the
# variable propagates into jitcommon and Zydis, which are added by
# x86port's CMakeLists and would otherwise fail the same way.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${X2_X86PORT_DIR}" "${CMAKE_BINARY_DIR}/x86port"
                 EXCLUDE_FROM_ALL)
if(NOT TARGET x86port_runtime)
    message(FATAL_ERROR
        "x2native: shared/x86port does not provide the required "
        "product x86port_runtime target. The retired static product "
        "has been removed and is not a fallback; update shared/x86port.")
endif()

# Tests compile title-native call adapters directly. Any target that includes
# the title x86 ABI also consumes the canonical x86port CPU type and must link
# the same product runtime rather than growing a test-local state model.
get_property(_x2_local_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_x2_target IN LISTS _x2_local_targets)
    get_target_property(_x2_target_includes "${_x2_target}" INCLUDE_DIRECTORIES)
    if(_x2_target_includes MATCHES "src/runtime/x86_abi")
        target_link_libraries("${_x2_target}" PRIVATE x86port_runtime)
    endif()
endforeach()

add_executable(test_alchemy_controller_adapter
    tests/test_alchemy_controller_adapter.cpp
    src/input/ig_controller_manager_adapter.cpp)
target_include_directories(test_alchemy_controller_adapter PRIVATE src/input)
target_link_libraries(test_alchemy_controller_adapter PRIVATE alchemy::input)
target_compile_options(test_alchemy_controller_adapter PRIVATE -UNDEBUG)
add_test(NAME alchemy_controller_adapter COMMAND test_alchemy_controller_adapter)

add_dependencies(x2native x2_shaders x2_pad_glyph_header
                 x2_prompt_glyph_atlas)
target_include_directories(x2native PRIVATE src/runtime/x86_abi src/native src/vulkan
                                            src/config src/presentation src/ui
                                            src/input src/save src/audio
                                            src/media src/gpu src/d3d8 src/gen
                                            ${CMAKE_BINARY_DIR})
target_link_libraries(x2native PRIVATE x2_rmlui_ui lucent::lucent lucent::zip)
target_link_libraries(x2native PRIVATE x86port_runtime)
target_link_libraries(x2native PRIVATE alchemy::input)

# Native overrides are declared in C where they belong. Every direct or
# indirect guest call reaches the runtime dispatcher's override slot; no
# generated wiring, wrappers, or isolated guest source bodies exist.


target_compile_definitions(x2native PRIVATE X2_WITH_SDL _GNU_SOURCE)
target_compile_definitions(x2native PRIVATE X86_NATIVE)
# PIE, so this binary does not sit at 0x400000 -- which is exactly where
# XMen2.exe wants to be. A non-PIE host takes the guest's preferred base
# and forces it to relocate for no reason.
set_target_properties(x2native PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT APPLE AND NOT ANDROID)
    target_link_options(x2native PRIVATE -pie)
endif()
# libm: the exe's x87 translations use atan2l/fmodl and friends.
# -pthread: the heartbeat (src/native/heartbeat.c) runs on its own thread
# so that a guest stuck anywhere still gets a liveness line.
target_link_libraries(x2native PRIVATE ${X2_SDL} ${X2_FFMPEG_TARGET}
                                       x2_runtime_config m Threads::Threads)
if(ANDROID)
    set_target_properties(x2native PROPERTIES OUTPUT_NAME main)
    get_filename_component(_x2_android_ndk_version "${CMAKE_ANDROID_NDK}" NAME)
    if(NOT _x2_android_ndk_version)
        message(FATAL_ERROR "x2native: Android toolchain did not expose its NDK version")
    endif()
    # Gradle consumes this generated contract. It keeps the Android shell
    # independent of CMake's internal target names while still refusing a
    # stale or hand-pointed-at native library.
    set(_x2_android_properties
        "x2.nativeLibrary=$<TARGET_FILE:x2native>\n"
        "x2.sdlLibrary=$<TARGET_FILE:${X2_SDL}>\n"
        "x2.cxxLibrary=${X2_ANDROID_CXX_LIBRARY}\n"
        "x2.androidAbi=${ANDROID_ABI}\n"
        "x2.androidPlatform=${ANDROID_PLATFORM}\n"
        "x2.ndkVersion=${_x2_android_ndk_version}\n"
        "x2.sdlJavaDir=${X2_SDL_ANDROID_JAVA_DIR}\n"
        "x2.lucentJavaDir=${X2_LUCENT_ANDROID_JAVA_DIR}\n"
        "x2.uiDir=${CMAKE_BINARY_DIR}/ui\n")
    string(JOIN "" _x2_android_properties ${_x2_android_properties})
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/x2-android.properties"
         CONTENT "${_x2_android_properties}")
endif()
# No $ENV{} here: CMake would bake in whatever GAME_PC_DIR was at CONFIGURE
# time, which is how this test first "failed" against an empty path. x2native
# reads it itself at run time and exits 77 (ctest SKIP) if it is unset,
# saying so -- a skip that announces itself, not a silent pass.
add_test(NAME native_battery
         COMMAND x2native --no-window --selftest)
set_tests_properties(native_battery PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME runtime_boundary_binary
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/runtime_boundary.py
                 --binary $<TARGET_FILE:x2native>)
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
