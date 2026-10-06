cmake_minimum_required(VERSION 3.16)
project(xmen2_sdl LANGUAGES C CXX)

# Every target is position-independent and every executable links as a PIE.
# x2native (on Android a shared object) needs each static library it pulls
# in to be PIC -- x86port, jitcommon and Zydis included, which is why this is
# the consumer's setting and not x86port's. The tests need the same link
# mode for a different reason: a non-PIE executable sits at 0x00400000 and
# the kernel randomises its brk heap upward from there, into the guest
# addresses the identity-mapped tests claim. Measured: the heap at
# 0x3106c000 failed test_behaved_context's 0x31000000 arena 3 runs in 500.
# x2native itself would otherwise sit at 0x00400000, exactly where XMen2.exe
# wants to be, and force it to relocate. check_pie_supported() is what makes
# POSITION_INDEPENDENT_CODE add the PIE link flag to executables.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CheckPIESupported)
check_pie_supported(LANGUAGES C CXX)

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

if(MINGW)
    # MinGW-w64 declares gmtime_r and localtime_r only when asked for them.
    add_compile_definitions(_POSIX_THREAD_SAFE_FUNCTIONS=200112L)
endif()

# Shared port infrastructure: the desktop picker uses Lucent's safe, tested
# archive extraction rather than carrying a second ZIP implementation here.
include(FetchContent)
set(LUCENT_BUILD_ZIP ON)
FetchContent_Declare(lucent
    GIT_REPOSITORY https://github.com/SomeoneIsWorking/lucent.git
    GIT_TAG f592a5c9797e1181622e2cff9916d8c4d49a2b71
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(lucent)

# The Windows halves of the src/native/platform_* boundaries; POSIX hosts
# implement them inline in the headers.
if(WIN32)
    add_library(x2_platform STATIC
        src/native/platform_mman_win32.cpp
        src/native/platform_posix_win32.cpp)
    target_include_directories(x2_platform PUBLIC src/native)
    target_link_libraries(x2_platform PUBLIC ws2_32 iphlpapi)
endif()

# Unit executables compile shipping subsystem sources directly, so they must
# use the same config/logger owners as the product rather than gaining test
# replacements. This directory default is cleared before external shared
# projects are added below.

add_library(x2_runtime_services STATIC
    src/config/environment.cpp
    src/diagnostics/crash_report.cpp
    src/diagnostics/diagnostics_startup.cpp
    src/diagnostics/host_code_location.cpp
    src/diagnostics/run_log.cpp
    src/native/x2_log.cpp)
set_target_properties(x2_runtime_services PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(x2_runtime_services PUBLIC src/config src/diagnostics
                           src/native)
target_link_libraries(x2_runtime_services PUBLIC lucent::lucent)
if(WIN32)
    target_link_libraries(x2_runtime_services PUBLIC x2_platform)
endif()
link_libraries(x2_runtime_services)

add_library(x2_runtime_config STATIC
    src/config/config_directory.cpp
    src/config/runtime_cvars.cpp)
set_target_properties(x2_runtime_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(x2_runtime_config PUBLIC src/config)
target_link_libraries(x2_runtime_config PUBLIC x2_runtime_services
                                               lucent::lucent)

add_executable(test_x2_log tests/test_x2_log.cpp)
target_link_libraries(test_x2_log PRIVATE x2_runtime_services)
add_test(NAME x2_log COMMAND test_x2_log)

add_executable(test_run_log tests/test_run_log.cpp)
target_link_libraries(test_run_log PRIVATE x2_runtime_services)
add_test(NAME run_log
         COMMAND test_run_log ${CMAKE_CURRENT_BINARY_DIR}/test-run-log)

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
if(EMSCRIPTEN)
    # WebAssembly SIMD, for every target this tree compiles, x86port included.
    # Every browser with WebGPU -- which the product requires -- executes it,
    # and without it the compiler cannot vectorise a single host loop: the VS
    # 1.1 executor's 64-lane rows ran one float at a time. It changes no
    # result: the vectoriser keeps IEEE semantics without -ffast-math.
    add_compile_options(-msimd128)
    add_link_options(-msimd128)
    include(cmake/WebDependencies.cmake)
elseif(ANDROID)
    # Android ships the whole port as one shared object, so every cross-module
    # call inside it -- including the JIT's dispatch and helper routing --
    # defaults to going out through a PLT stub and back. Measured on the
    # arm64-v8a build: 5,290 JUMP_SLOT relocations, 4,709 of which resolve to a
    # function defined in this same library, and 8,893 exported dynamic
    # symbols for the linker to bind at startup. Hiding by default turns those
    # into direct branches; -Bsymbolic-functions binds whatever still needs a
    # dynamic symbol to the local definition. The entry point that Android
    # actually looks up keeps explicit default visibility (src/native/
    # x2native.cpp), JNI entries keep theirs through JNIEXPORT, and
    # verify_native_entry refuses a library that stops exporting `main`.
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
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
    set(X2_ANDROID_FRAMEWORK_JAVA_DIR
        "${X2_ANDROID_PORT_PREFIX}/share/android-port/framework-java")
    if(NOT IS_DIRECTORY "${X2_ANDROID_FRAMEWORK_JAVA_DIR}")
        message(FATAL_ERROR "x2native: shared android-port prefix omitted Android framework Java sources")
    endif()
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
elseif(WIN32)
    # tools/build_windows.py configures with the llvm-mingw toolchain and the
    # static prefix tools/windows_deps.py builds; nothing comes from the host.
    find_package(Freetype CONFIG REQUIRED)
    find_package(SDL3 CONFIG REQUIRED)
    set(X2_SDL SDL3::SDL3-static)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X2_FFMPEG REQUIRED IMPORTED_TARGET
                      libavformat libavcodec libavutil libswscale libswresample)
    set(X2_FFMPEG_TARGET PkgConfig::X2_FFMPEG)
else()
    # The runtime letters keyboard prompts in the shared key typeface.
    find_package(Freetype REQUIRED)
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

add_executable(test_assertions_enabled tests/test_assertions_enabled.cpp)
add_test(NAME assertions_enabled COMMAND test_assertions_enabled)

add_executable(test_config_directory tests/test_config_directory.cpp
               src/config/config_directory.cpp)
target_include_directories(test_config_directory PRIVATE src/config)
target_link_libraries(test_config_directory PRIVATE lucent::lucent)
add_test(NAME config_directory COMMAND test_config_directory)

add_executable(test_runtime_cvars tests/test_runtime_cvars.cpp
               src/config/runtime_cvars.cpp src/config/config_directory.cpp)
target_include_directories(test_runtime_cvars PRIVATE src/config)
target_link_libraries(test_runtime_cvars PRIVATE lucent::lucent)
add_test(NAME runtime_cvars COMMAND test_runtime_cvars)
set_tests_properties(runtime_cvars PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_install_picker tests/test_install_picker.cpp
               src/native/install_picker.cpp src/native/install_archive.cpp
               src/native/install_validation.cpp src/native/pe_header.cpp
               src/config/config_directory.cpp)
target_include_directories(test_install_picker PRIVATE src/native src/config
                           ${CMAKE_BINARY_DIR})
target_link_libraries(test_install_picker PRIVATE ${X2_SDL} lucent::lucent lucent::zip)
add_test(NAME install_picker COMMAND test_install_picker)

add_executable(test_install_archive tests/test_install_archive.cpp
               src/native/install_archive.cpp src/native/install_validation.cpp
               src/native/pe_header.cpp
               src/config/config_directory.cpp)
target_include_directories(test_install_archive PRIVATE src/native src/config
                            ${CMAKE_BINARY_DIR})
target_link_libraries(test_install_archive PRIVATE lucent::lucent lucent::zip)
add_test(NAME install_archive COMMAND test_install_archive)

# The web entry point's launch routing, unit-tested natively: the shared
# owner is pure argv, so the falsifier needs no browser (issue #151).
add_executable(test_web_request tests/test_web_request.cpp src/web/web_request.cpp)
target_include_directories(test_web_request PRIVATE src/web)
add_test(NAME web_request COMMAND test_web_request)

add_executable(test_winsock_host tests/test_winsock_host.cpp
               src/native/winsock_host.cpp src/native/winsock_resolve.cpp)
add_test(NAME winsock_host COMMAND test_winsock_host)

add_executable(test_control_http tests/test_control_http.cpp
               src/native/control_http.cpp)
target_include_directories(test_control_http PRIVATE src/native)
target_link_libraries(test_control_http PRIVATE pthread)
add_test(NAME control_http COMMAND test_control_http)

add_executable(test_control_query tests/test_control_query.cpp
               src/native/control_query.cpp)
target_include_directories(test_control_query PRIVATE src/native)
add_test(NAME control_query COMMAND test_control_query)

add_executable(test_lan_presence_protocol tests/test_lan_presence_protocol.cpp
               src/net/lan_presence_protocol.cpp)
add_test(NAME lan_presence_protocol COMMAND test_lan_presence_protocol)

add_executable(test_lan_presence_policy tests/test_lan_presence_policy.cpp
               src/net/lan_presence_policy.cpp)
add_test(NAME lan_presence_policy COMMAND test_lan_presence_policy)

add_executable(test_lan_rendezvous tests/test_lan_rendezvous.cpp
               src/net/lan_rendezvous.cpp)
add_test(NAME lan_rendezvous COMMAND test_lan_rendezvous)

add_executable(test_touch_controls tests/test_touch_controls.cpp
               src/input/touch_controls.cpp
               src/input/thumb_stick.cpp
               src/presentation/touch_layout.cpp)
target_include_directories(test_touch_controls PRIVATE src/input)
target_link_libraries(test_touch_controls PRIVATE lucent::lucent)
add_test(NAME touch_controls COMMAND test_touch_controls)

add_executable(test_thumb_stick tests/test_thumb_stick.cpp
               src/input/thumb_stick.cpp)
target_link_libraries(test_thumb_stick PRIVATE lucent::lucent)
add_test(NAME thumb_stick COMMAND test_thumb_stick)

# A physical stick's radial dead zone (the pad sample's), header-only.
add_executable(test_pad_stick_dead_zone tests/test_pad_stick_dead_zone.cpp)
target_include_directories(test_pad_stick_dead_zone PRIVATE src/native)
target_link_libraries(test_pad_stick_dead_zone PRIVATE m)
add_test(NAME pad_stick_dead_zone COMMAND test_pad_stick_dead_zone)

# The only touch owner that reaches the pad the game reads, so it is the only
# one whose failure is invisible to the others' tests. Needs SDL for the window
# and the synthetic joystick, so it SKIPs (77) where neither exists.
add_executable(test_touch_runtime tests/test_touch_runtime.cpp
               src/input/touch_runtime.cpp
               src/input/touch_skip_button.cpp
               src/input/cutscene_skip.cpp
               src/input/touch_pad_publisher.cpp
               src/input/touch_menu_controls.cpp
               src/input/touch_pointer.cpp
               src/input/touch_visuals.cpp
               src/input/directinput_controller_sample.cpp
               src/input/touch_census.cpp
               src/input/touch_pad.cpp
               src/native/host_touch.cpp
               src/native/dinput_pad_report.cpp
               src/input/touch_controls.cpp
               src/input/thumb_stick.cpp
               src/input/touch_source.cpp
               src/input/gameplay_control.cpp
               src/input/transient_controller_assignment.cpp
               src/input/controller_instance.cpp
               src/presentation/touch_layout.cpp
               src/native/dinput_pad.cpp
               src/native/dinput_pad_subsystem.cpp
               src/native/dinput_pad_identity.cpp
               src/native/dinput_pad_sample.cpp
               src/native/dinput_pad_virtual.cpp
               src/native/dinput_pad_virtual_actions.cpp
               src/native/dinput_pad_virtual_device.cpp
               src/native/dinput_pad_virtual_reader.cpp
               src/native/guest_clock.cpp
               src/native/x2_log.cpp
               src/config/settings.cpp
               src/config/settings_store.cpp
               src/config/config_directory.cpp
               src/config/directory_create.cpp
               src/config/environment.cpp
               src/config/hud_settings.cpp
               src/config/input_assignments.cpp
               src/config/boot_mode.cpp)
target_include_directories(test_touch_runtime PRIVATE
                           src/input src/native src/config src/presentation)
target_compile_definitions(test_touch_runtime PRIVATE X2_WITH_SDL
    X2_TEST_TOUCH_RUNTIME_ROOT="${CMAKE_BINARY_DIR}/test-touch-runtime")
target_link_libraries(test_touch_runtime PRIVATE lucent::lucent ${X2_SDL})
add_test(NAME touch_runtime COMMAND test_touch_runtime)
set_tests_properties(touch_runtime PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(test_hud_portrait_position tests/test_hud_portrait_position.cpp
               src/native/hud_portrait_position.cpp)
target_include_directories(test_hud_portrait_position PRIVATE src/native)
add_test(NAME hud_portrait_position COMMAND test_hud_portrait_position)

add_executable(test_gameplay_control tests/test_gameplay_control.cpp
               src/input/gameplay_control.cpp)
add_test(NAME gameplay_control COMMAND test_gameplay_control)

# The cinematic skip a finger asks for: offered and taken by the cutscene
# player, placed and pressed by the touch button.
add_executable(test_touch_skip_button tests/test_touch_skip_button.cpp
               src/input/touch_skip_button.cpp src/input/cutscene_skip.cpp)
target_link_libraries(test_touch_skip_button PRIVATE lucent::lucent)
add_test(NAME touch_skip_button COMMAND test_touch_skip_button)

add_executable(test_macos_bundle tests/test_macos_bundle.cpp
               src/native/macos_bundle.cpp src/config/environment.cpp)
target_include_directories(test_macos_bundle PRIVATE src/native)
target_link_libraries(test_macos_bundle PRIVATE ${X2_SDL})
add_test(NAME macos_bundle COMMAND test_macos_bundle)

add_executable(test_touch_source tests/test_touch_source.cpp
               src/input/touch_source.cpp)
target_link_libraries(test_touch_source PRIVATE ${X2_SDL})
add_test(NAME touch_source COMMAND test_touch_source)

add_executable(test_touch_layout tests/test_touch_layout.cpp
               src/presentation/touch_layout.cpp)
add_test(NAME touch_layout COMMAND test_touch_layout)

add_executable(test_hud_layout tests/test_hud_layout.cpp
               src/presentation/hud_layout.cpp
               src/presentation/touch_layout.cpp
               src/config/hud_settings.cpp)
target_include_directories(test_hud_layout PRIVATE src/presentation src/config)
target_link_libraries(test_hud_layout PRIVATE m)
add_test(NAME hud_layout COMMAND test_hud_layout)

add_executable(test_gpu_pass_binds tests/test_gpu_pass_binds.cpp
               src/gpu/gpu_pass_binds.cpp)
target_include_directories(test_gpu_pass_binds PRIVATE src/gpu)
add_test(NAME gpu_pass_binds COMMAND test_gpu_pass_binds)

add_executable(test_gpu_index_arena tests/test_gpu_index_arena.cpp
               src/gpu/gpu_index_arena.cpp)
target_include_directories(test_gpu_index_arena PRIVATE src/gpu)
add_test(NAME gpu_index_arena COMMAND test_gpu_index_arena)

add_executable(test_d3d8_index_max tests/test_d3d8_index_max.cpp
               src/d3d8/d3d8_draw_range.cpp)
target_include_directories(test_d3d8_index_max PRIVATE src/d3d8 src/gpu
                           src/native src/runtime/x86_abi)
add_test(NAME d3d8_index_max COMMAND test_d3d8_index_max)

add_executable(test_gpu_frame_timing tests/test_gpu_frame_timing.cpp
               src/gpu/gpu_frame_timing.cpp)
target_include_directories(test_gpu_frame_timing PRIVATE src/gpu)
add_test(NAME gpu_frame_timing COMMAND test_gpu_frame_timing)

add_executable(test_present_luma tests/test_present_luma.cpp
               src/gpu/gpu_present_luma.cpp src/native/x2_log.cpp)
target_include_directories(test_present_luma PRIVATE src/gpu)
target_link_libraries(test_present_luma PRIVATE lucent::lucent)
add_test(NAME present_luma COMMAND test_present_luma)

add_executable(test_fmv_policy tests/test_fmv_policy.cpp
               src/media/fmv_policy.cpp)
target_include_directories(test_fmv_policy PRIVATE src/media)
add_test(NAME fmv_policy COMMAND test_fmv_policy)

add_executable(test_fmv_timing tests/test_fmv_timing.cpp
               src/media/fmv_timing.cpp)
target_include_directories(test_fmv_timing PRIVATE src/media)
target_link_libraries(test_fmv_timing PRIVATE m)
add_test(NAME fmv_timing COMMAND test_fmv_timing)

add_executable(test_fmv_probe tests/test_fmv_probe.cpp src/media/fmv_probe.cpp)
target_include_directories(test_fmv_probe PRIVATE src/media)
add_test(NAME fmv_probe COMMAND test_fmv_probe)

add_executable(test_fmv_decoder_drain tests/test_fmv_decoder_drain.cpp
               src/media/fmv_decoder_drain.cpp)
target_include_directories(test_fmv_decoder_drain PRIVATE src/media)
add_test(NAME fmv_decoder_drain COMMAND test_fmv_decoder_drain)

add_executable(test_d3d8_texture_luma tests/test_d3d8_texture_luma.cpp
               src/d3d8/d3d8_texture_luma.cpp src/config/environment.cpp)
target_include_directories(test_d3d8_texture_luma PRIVATE src/d3d8 src/config)
add_test(NAME d3d8_texture_luma COMMAND test_d3d8_texture_luma)

add_executable(test_gpu_texture_format tests/test_gpu_texture_format.cpp
    src/gpu/gpu_texture_format.cpp)
target_include_directories(test_gpu_texture_format PRIVATE src/gpu)
add_test(NAME gpu_texture_format COMMAND test_gpu_texture_format)

add_executable(test_movie_audio tests/test_movie_audio.cpp
               src/audio/movie_audio.cpp)
target_include_directories(test_movie_audio PRIVATE src/audio)
target_link_libraries(test_movie_audio PRIVATE m pthread)
add_test(NAME movie_audio COMMAND test_movie_audio)

add_executable(test_cutscene_script_audio tests/test_cutscene_script_audio.cpp
               src/native/cutscene_script_audio.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_cutscene_script_audio PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_cutscene_script_audio PRIVATE X86_NATIVE)
add_test(NAME cutscene_script_audio COMMAND test_cutscene_script_audio)

add_executable(test_behaved_context tests/test_behaved_context.cpp
               src/native/behaved_context.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_behaved_context PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_behaved_context PRIVATE X86_NATIVE)
add_test(NAME behaved_context COMMAND test_behaved_context)

add_executable(test_movie_image_layout tests/test_movie_image_layout.cpp
               src/native/movie_image_layout.cpp)
target_include_directories(test_movie_image_layout PRIVATE src/native)
add_test(NAME movie_image_layout COMMAND test_movie_image_layout)

add_executable(test_win_path tests/test_win_path.cpp src/native/win_path.cpp
    src/native/host_dir_cache.cpp)
target_include_directories(test_win_path PRIVATE src/native src/save)
target_link_libraries(test_win_path PRIVATE x2_runtime_config)
target_compile_definitions(test_win_path PRIVATE
    X2_TEST_WIN_PATH_ROOT="${CMAKE_BINARY_DIR}/test-win-path")
add_test(NAME win_path COMMAND test_win_path)

add_executable(test_platform_file_map tests/test_platform_file_map.cpp
               src/native/platform_file_map.cpp)
target_include_directories(test_platform_file_map PRIVATE src/native)
add_test(NAME platform_file_map COMMAND test_platform_file_map)

add_executable(test_fmv_decode tests/test_fmv_decode.cpp
               src/media/fmv_player.cpp src/media/fmv_audio_decode.cpp
               src/media/fmv_decoder_drain.cpp src/media/fmv_policy.cpp
               src/media/fmv_probe.cpp src/media/fmv_sfd.cpp src/media/fmv_timing.cpp)
target_include_directories(test_fmv_decode PRIVATE src/media)
    target_link_libraries(test_fmv_decode PRIVATE ${X2_FFMPEG_TARGET} m)
add_test(NAME fmv_decode COMMAND test_fmv_decode)
set_tests_properties(fmv_decode PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME structure
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_structure.py)
add_test(NAME structure_selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_structure.py --selftest)
# Touch play is a device feature on every platform, never a package's. This
# refuses a platform branch in a touch owner, and refuses a pass that inspected
# nothing. See docs/touch-play.md.
add_test(NAME touch_portable
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_touch_portable.py)
add_test(NAME touch_portable_selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_touch_portable.py --selftest)
# What of the engine compiles to wasm32, with denominators. It SKIPs (77) with
# no Emscripten toolchain rather than reporting a pass it did not measure; CI
# passes --require-emsdk so absence is a failure there. See docs/web-release.md.
add_test(NAME wasm_portability
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/wasm_portability.py)
set_tests_properties(wasm_portability PROPERTIES SKIP_RETURN_CODE 77
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME project_state
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/project_state.py
                 --root ${CMAKE_SOURCE_DIR})
set_tests_properties(project_state PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     SKIP_RETURN_CODE 77)
if(NOT WIN32)
    add_executable(test_live_session tests/test_live_session.cpp
                   src/native/live_session.cpp src/native/json_string.cpp
                   src/native/host_dir_cache.cpp
                   src/config/directory_create.cpp)
    target_include_directories(test_live_session PRIVATE src/native src/config)
    target_compile_definitions(test_live_session PRIVATE
        X2_TEST_LIVE_SESSION_DIR="${CMAKE_BINARY_DIR}/test-live-session")
    add_test(NAME live_session COMMAND test_live_session)
endif()

add_executable(test_settings tests/test_settings.cpp src/config/settings.cpp src/config/hud_settings.cpp
               src/config/input_assignments.cpp
               src/config/boot_mode.cpp)
target_include_directories(test_settings PRIVATE src/config)
target_compile_definitions(test_settings PRIVATE
    X2_TEST_SETTINGS_PATH="${CMAKE_BINARY_DIR}/test-x2native.conf")
add_test(NAME settings COMMAND test_settings)

add_executable(test_settings_overlay tests/test_settings_overlay.cpp
                src/ui/settings_overlay_state.cpp)
target_include_directories(test_settings_overlay PRIVATE src/ui)
target_link_libraries(test_settings_overlay PRIVATE ${X2_SDL})
add_test(NAME settings_overlay COMMAND test_settings_overlay)

add_executable(test_display_mode_seed tests/test_display_mode_seed.cpp
                src/presentation/display_mode_seed.cpp
                src/presentation/display_geometry.cpp
                src/presentation/resolution_ladder.cpp)
target_include_directories(test_display_mode_seed PRIVATE src/presentation
                          src/native src/config)
add_test(NAME display_mode_seed COMMAND test_display_mode_seed)

add_executable(test_boot_mode_policy tests/test_boot_mode_policy.cpp
               src/native/boot_mode_policy.cpp)
target_include_directories(test_boot_mode_policy PRIVATE src/config src/native)
add_test(NAME boot_mode_policy COMMAND test_boot_mode_policy)

add_executable(test_boot_mode_runtime tests/test_boot_mode_runtime.cpp
               src/native/boot_mode_runtime.cpp src/native/boot_mode_policy.cpp
               src/save/save_catalog.cpp src/save/save_directory.cpp)
target_include_directories(test_boot_mode_runtime PRIVATE src/config src/native
                                                        src/save)
target_compile_definitions(test_boot_mode_runtime PRIVATE
    X2_TEST_BOOT_STORAGE_ROOT="${CMAKE_BINARY_DIR}/test-boot-storage")
add_test(NAME boot_mode_runtime COMMAND test_boot_mode_runtime)

add_executable(test_boot_menu_transition tests/test_boot_menu_transition.cpp
               src/native/boot_menu_transition.cpp)
target_include_directories(test_boot_menu_transition PRIVATE src/native src/runtime/x86_abi)
add_test(NAME boot_menu_transition COMMAND test_boot_menu_transition)

add_executable(test_x86_guest_call_stack tests/test_x86_guest_call_stack.cpp
               src/native/x86_guest_call_stack.cpp)
target_include_directories(test_x86_guest_call_stack PRIVATE src/native)
target_link_libraries(test_x86_guest_call_stack PRIVATE Threads::Threads)
add_test(NAME x86_guest_call_stack COMMAND test_x86_guest_call_stack)

add_executable(test_jit_intercept tests/test_jit_intercept.cpp
               src/native/x86_engine_intercept.cpp
               src/native/x86_guest_call_stack.cpp)
target_include_directories(test_jit_intercept PRIVATE src/native src/runtime/x86_abi)
target_link_libraries(test_jit_intercept PRIVATE x86port_runtime)
add_test(NAME jit_intercept COMMAND test_jit_intercept)

add_executable(test_x86_engine_diagnostic tests/test_x86_engine_diagnostic.cpp
               src/native/x86_engine_diagnostic.cpp)
target_include_directories(test_x86_engine_diagnostic PRIVATE src/native
                       src/runtime/x86_abi)
target_link_libraries(test_x86_engine_diagnostic PRIVATE x86port_runtime)
add_test(NAME x86_engine_diagnostic COMMAND test_x86_engine_diagnostic)

add_executable(test_engine_hot_blocks tests/test_engine_hot_blocks.cpp
               src/native/x86_engine_dispatch_report.cpp)
target_include_directories(test_engine_hot_blocks PRIVATE src/native
                       src/runtime/x86_abi)
target_link_libraries(test_engine_hot_blocks PRIVATE x86port_runtime)
add_test(NAME engine_hot_blocks COMMAND test_engine_hot_blocks)

add_executable(test_joystick_neutral tests/test_joystick_neutral.cpp
               src/input/joystick_neutral.cpp)
target_include_directories(test_joystick_neutral PRIVATE src/input)
add_test(NAME joystick_neutral COMMAND test_joystick_neutral)

add_executable(test_aspect_fit tests/test_aspect_fit.cpp
               src/presentation/aspect_fit.cpp)
target_include_directories(test_aspect_fit PRIVATE src/presentation)
add_test(NAME aspect_fit COMMAND test_aspect_fit)

add_executable(test_win32_mouse tests/test_win32_mouse.cpp
               src/native/win32_mouse.cpp
               src/presentation/aspect_fit.cpp)
target_include_directories(test_win32_mouse PRIVATE src/native src/presentation)
add_test(NAME win32_mouse COMMAND test_win32_mouse)

# GetCursorPos against the WM_MOUSEMOVE the same cursor produced, through the
# shipping pointer translation and a dummy-driver SDL window.
add_executable(test_win32_pointer tests/test_win32_pointer.cpp
               src/native/win32_pointer.cpp
               src/native/win32_mouse.cpp
               src/presentation/aspect_fit.cpp
               src/config/settings.cpp
               src/config/settings_store.cpp
               src/config/hud_settings.cpp
               src/config/input_assignments.cpp
               src/config/boot_mode.cpp
               src/native/x2_log.cpp)
target_include_directories(test_win32_pointer PRIVATE
                           src/native src/config src/presentation src/input)
target_compile_definitions(test_win32_pointer PRIVATE X2_WITH_SDL
    X2_TEST_WIN32_POINTER_ROOT="${CMAKE_BINARY_DIR}/test-win32-pointer")
target_link_libraries(test_win32_pointer PRIVATE lucent::lucent ${X2_SDL})
add_test(NAME win32_pointer COMMAND test_win32_pointer)
set_tests_properties(win32_pointer PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_d3d8_live_resolution
               tests/test_d3d8_live_resolution.cpp
               src/d3d8/d3d8_live_resolution.cpp)
target_include_directories(test_d3d8_live_resolution PRIVATE src/d3d8 src/gpu)
add_test(NAME d3d8_live_resolution COMMAND test_d3d8_live_resolution)

add_executable(test_dialog_selection_scale_policy
               tests/test_dialog_selection_scale_policy.cpp
               src/native/dialog_selection_scale_policy.cpp)
target_include_directories(test_dialog_selection_scale_policy PRIVATE
                           src/native src/presentation)
target_link_libraries(test_dialog_selection_scale_policy PRIVATE m)
add_test(NAME dialog_selection_scale_policy
         COMMAND test_dialog_selection_scale_policy)

add_executable(test_d3d8_selector_probe
               tests/test_d3d8_selector_probe.cpp
               src/d3d8/d3d8_selector_probe.cpp
               src/d3d8/d3d8_selector_probe_json.cpp
               src/d3d8/d3d8_texture_provenance.cpp)
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

add_executable(test_live_resolution tests/test_live_resolution.cpp
               src/presentation/live_resolution.cpp
               src/presentation/resolution_ladder.cpp)
target_include_directories(test_live_resolution PRIVATE
                           src/presentation src/config src/d3d8 src/native)
add_test(NAME live_resolution COMMAND test_live_resolution)

add_executable(test_load_game_menu_policy
               tests/test_load_game_menu_policy.cpp
               src/save/load_game_menu_policy.cpp)
target_include_directories(test_load_game_menu_policy PRIVATE src/save)
add_test(NAME load_game_menu_policy COMMAND test_load_game_menu_policy)
add_executable(test_gpu_present_resize tests/test_gpu_present_resize.cpp
               src/gpu/gpu_present.cpp
               src/presentation/aspect_fit.cpp)
target_include_directories(test_gpu_present_resize PRIVATE src/gpu
                           src/presentation)
target_compile_definitions(test_gpu_present_resize PRIVATE
    SDL_CreateGPUTexture=x2_test_create_gpu_texture
    SDL_ReleaseGPUTexture=x2_test_release_gpu_texture)
target_link_libraries(test_gpu_present_resize PRIVATE ${X2_SDL})
add_test(NAME gpu_present_resize COMMAND test_gpu_present_resize)

add_executable(test_gpu_depth_binding tests/test_gpu_depth_binding.cpp
               src/gpu/gpu_depth_binding.cpp)
target_include_directories(test_gpu_depth_binding PRIVATE src/gpu)
target_compile_definitions(test_gpu_depth_binding PRIVATE X2_WITH_SDL=1)
foreach(_gpu_call GPUTextureSupportsFormat CreateGPUTexture CreateGPUSampler
                  AcquireGPUCommandBuffer BeginGPURenderPass EndGPURenderPass
                  SubmitGPUCommandBuffer CancelGPUCommandBuffer ReleaseGPUTexture
                  ReleaseGPUSampler)
    target_compile_definitions(test_gpu_depth_binding PRIVATE
        SDL_${_gpu_call}=x2_test_${_gpu_call})
endforeach()
target_link_libraries(test_gpu_depth_binding PRIVATE ${X2_SDL})
add_test(NAME gpu_depth_binding COMMAND test_gpu_depth_binding)

add_test(NAME selector_probe_tool
         COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_selector_probe)
set_tests_properties(selector_probe_tool PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_shadow_policy tests/test_shadow_policy.cpp
               src/gpu/gpu_matrix.cpp src/gpu/shadow_policy.cpp)
target_include_directories(test_shadow_policy PRIVATE src/gpu)
target_link_libraries(test_shadow_policy PRIVATE m)
add_test(NAME shadow_policy COMMAND test_shadow_policy)

add_executable(test_player_input tests/test_player_input.cpp
               src/input/player_input.cpp src/input/player_participation_policy.cpp
               src/config/settings.cpp src/config/hud_settings.cpp src/config/input_assignments.cpp
               src/config/boot_mode.cpp
               src/native/input_binding_sets.cpp
               src/native/xbox_defaults.cpp)
target_include_directories(test_player_input PRIVATE src/runtime/x86_abi src/native
                                                   src/config src/input)
add_test(NAME player_input COMMAND test_player_input)

add_executable(test_player_participation_policy
               tests/test_player_participation_policy.cpp
               src/input/player_participation_policy.cpp)
target_include_directories(test_player_participation_policy PRIVATE src/input)
add_test(NAME player_participation_policy
         COMMAND test_player_participation_policy)

add_executable(test_controller_hotplug tests/test_controller_hotplug.cpp
               src/input/controller_hotplug.cpp)
target_include_directories(test_controller_hotplug PRIVATE src/input)
add_test(NAME controller_hotplug COMMAND test_controller_hotplug)

add_executable(test_controller_instance tests/test_controller_instance.cpp
               src/input/controller_instance.cpp)
target_include_directories(test_controller_instance PRIVATE src/input src/native)
add_test(NAME controller_instance COMMAND test_controller_instance)

add_executable(test_dinput8_controller_slots
               tests/test_dinput8_controller_slots.cpp
               src/native/dinput8_controller_slots.cpp
               src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_dinput8_controller_slots PRIVATE src/native)
target_link_libraries(test_dinput8_controller_slots PRIVATE pthread)
add_test(NAME dinput8_controller_slots COMMAND test_dinput8_controller_slots)
set_tests_properties(dinput8_controller_slots PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_transient_controller_assignment
               tests/test_transient_controller_assignment.cpp
               src/input/transient_controller_assignment.cpp
               src/input/controller_instance.cpp
               src/config/settings.cpp src/config/hud_settings.cpp src/config/input_assignments.cpp
               src/config/boot_mode.cpp)
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

add_executable(test_keyboard_bindings_document
               tests/test_keyboard_bindings_document.cpp
               src/ui/keyboard_bindings_document.cpp src/ui/rml_text.cpp
               src/input/binding_rows.cpp src/config/settings.cpp
               src/config/hud_settings.cpp src/config/input_assignments.cpp
               src/config/boot_mode.cpp)
target_include_directories(test_keyboard_bindings_document PRIVATE
                           src/ui src/input src/native src/config)
target_link_libraries(test_keyboard_bindings_document PRIVATE rmlui_core)
add_test(NAME keyboard_bindings_document
         COMMAND test_keyboard_bindings_document)

add_executable(test_input_record tests/test_input_record.cpp
               src/input/input_record.cpp src/native/json_string.cpp
               src/config/directory_create.cpp)
target_include_directories(test_input_record PRIVATE src/input src/native
                           src/config)
add_test(NAME input_record COMMAND test_input_record)
set_tests_properties(input_record PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_dinput_system tests/test_dinput_system.cpp
               src/native/dinput_system.cpp tests/native_stubs.cpp)
target_include_directories(test_dinput_system PRIVATE src/runtime/x86_abi src/native)
target_compile_definitions(test_dinput_system PRIVATE X2_WITH_SDL)
target_link_libraries(test_dinput_system PRIVATE ${X2_SDL})
add_test(NAME dinput_system COMMAND test_dinput_system)

add_executable(test_cutscene_skip_publication
               tests/test_cutscene_skip_publication.cpp
               src/native/cutscene_skip_publication.cpp)
target_include_directories(test_cutscene_skip_publication PRIVATE src/native)
add_test(NAME cutscene_skip_publication COMMAND test_cutscene_skip_publication)

add_executable(test_cutscene_player_policy
               tests/test_cutscene_player_policy.cpp
               src/native/cutscene_player_policy.cpp)
target_include_directories(test_cutscene_player_policy PRIVATE src/native)
add_test(NAME cutscene_player_policy COMMAND test_cutscene_player_policy)

add_executable(test_cutscene_dialogue
               tests/test_cutscene_dialogue.cpp
               src/native/cutscene_dialogue.cpp
               src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_cutscene_dialogue PRIVATE
                           src/native src/runtime/x86_abi)
target_compile_definitions(test_cutscene_dialogue PRIVATE X86_NATIVE)
target_link_libraries(test_cutscene_dialogue PRIVATE pthread)
add_test(NAME cutscene_dialogue COMMAND test_cutscene_dialogue)

add_executable(test_behaved_player_heap
               tests/test_behaved_player_heap.cpp
               src/native/behaved_player.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_behaved_player_heap PRIVATE
                           src/native src/runtime/x86_abi)
target_compile_definitions(test_behaved_player_heap PRIVATE X86_NATIVE)
target_link_libraries(test_behaved_player_heap PRIVATE pthread)
add_test(NAME behaved_player_heap COMMAND test_behaved_player_heap)

add_executable(test_cutscene_event_player
               tests/test_cutscene_event_player.cpp
               src/native/cutscene_event_player.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_cutscene_event_player PRIVATE
                           src/native src/runtime/x86_abi)
target_compile_definitions(test_cutscene_event_player PRIVATE X86_NATIVE)
target_link_libraries(test_cutscene_event_player PRIVATE pthread)
add_test(NAME cutscene_event_player COMMAND test_cutscene_event_player)

add_executable(test_binding_rows tests/test_binding_rows.cpp
               src/input/binding_rows.cpp)
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

# Keyboard prompts are lettered at runtime (the game localizes key names) in
# the shared key typeface, which ships beside the other UI resources.
execute_process(
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py" --print-key-font
    RESULT_VARIABLE X2_KEY_FONT_RESULT
    OUTPUT_VARIABLE X2_KEY_FONT
    ERROR_VARIABLE X2_KEY_FONT_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT X2_KEY_FONT_RESULT EQUAL 0 OR NOT EXISTS "${X2_KEY_FONT}")
    message(FATAL_ERROR
        "Refusing configuration: the shared key typeface did not resolve:\n"
        "${X2_KEY_FONT_ERROR}")
endif()
configure_file("${X2_KEY_FONT}" "${CMAKE_BINARY_DIR}/ui/NotoSans-Bold-keys.ttf"
               COPYONLY)

# The touch menu pad draws the controller's own buttons from the same shared
# Xbox set as the prompt atlas; tools/ui_resource_files.py lists the staged
# names for packaging.
set(X2_MENU_PAD_ICONS dpad_up dpad_down dpad_left dpad_right face_a face_b
                      face_x face_y lb rb)
execute_process(
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py" --print-pad-svgs
            ${X2_MENU_PAD_ICONS}
    RESULT_VARIABLE X2_MENU_PAD_RESULT
    OUTPUT_VARIABLE X2_MENU_PAD_SVGS
    ERROR_VARIABLE X2_MENU_PAD_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "\r\n" "\n" X2_MENU_PAD_SVGS "${X2_MENU_PAD_SVGS}")
string(REPLACE "\n" ";" X2_MENU_PAD_SVGS "${X2_MENU_PAD_SVGS}")
list(LENGTH X2_MENU_PAD_ICONS X2_MENU_PAD_ICON_COUNT)
list(LENGTH X2_MENU_PAD_SVGS X2_MENU_PAD_SVG_COUNT)
if(NOT X2_MENU_PAD_RESULT EQUAL 0 OR
   NOT X2_MENU_PAD_SVG_COUNT EQUAL X2_MENU_PAD_ICON_COUNT)
    message(FATAL_ERROR
        "Refusing configuration: resolved ${X2_MENU_PAD_SVG_COUNT} of "
        "${X2_MENU_PAD_ICON_COUNT} menu pad SVGs:\n${X2_MENU_PAD_ERROR}")
endif()
math(EXPR X2_MENU_PAD_LAST "${X2_MENU_PAD_ICON_COUNT} - 1")
foreach(x2_menu_pad_index RANGE ${X2_MENU_PAD_LAST})
    list(GET X2_MENU_PAD_ICONS ${x2_menu_pad_index} x2_menu_pad_icon)
    list(GET X2_MENU_PAD_SVGS ${x2_menu_pad_index} x2_menu_pad_svg)
    configure_file("${x2_menu_pad_svg}"
                   "${CMAKE_BINARY_DIR}/ui/pad_${x2_menu_pad_icon}.svg" COPYONLY)
endforeach()

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


# The gamepad inventory, against an SDL virtual pad. dinput_pad.cpp depends on
# nothing in the guest runtime -- it is SDL on one side and plain values on the
# other -- so it links straight into a test binary.
add_executable(test_dinput_pad tests/test_dinput_pad.cpp src/native/dinput_pad.cpp
               src/native/dinput_pad_subsystem.cpp
               src/native/dinput_pad_identity.cpp
               src/native/dinput_pad_sample.cpp
               src/native/dinput_pad_report.cpp
        src/native/dinput_pad_virtual.cpp src/native/dinput_pad_virtual_actions.cpp
        src/native/dinput_pad_virtual_device.cpp src/native/dinput_pad_virtual_reader.cpp src/native/guest_clock.cpp)
target_include_directories(test_dinput_pad PRIVATE src/native)
target_compile_definitions(test_dinput_pad PRIVATE X2_WITH_SDL)
target_link_libraries(test_dinput_pad PRIVATE ${X2_SDL})
add_test(NAME dinput_pad COMMAND test_dinput_pad)
# The test already returns 77 when SDL cannot give it a gamepad subsystem, a
# virtual joystick, or an empty inventory. Without this it reported those as
# failures, which is the opposite of what a skip is for.
set_tests_properties(dinput_pad PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_directinput_controller_sample
    tests/test_directinput_controller_sample.cpp
    src/input/directinput_controller_sample.cpp)
target_include_directories(test_directinput_controller_sample
    PRIVATE src/input src/native)
target_compile_options(test_directinput_controller_sample PRIVATE -UNDEBUG)
add_test(NAME directinput_controller_sample
         COMMAND test_directinput_controller_sample)

add_executable(test_pad_glyphs tests/test_pad_glyphs.cpp
               src/native/pad_glyphs.cpp
               src/native/prompt_glyphs.cpp
               src/native/prompt_labels.cpp src/native/input_binding_sets.cpp
               src/native/keycap_run.cpp src/native/keycap_labels.cpp
               src/native/prompt_glyph_atlas.cpp src/ui/ui_resources.cpp
               src/native/input_bindings.cpp
               src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
add_dependencies(test_pad_glyphs x2_pad_glyph_header x2_prompt_glyph_atlas)
target_include_directories(test_pad_glyphs PRIVATE src/runtime/x86_abi src/native
                                                  src/input src/gen
                                                  ${CMAKE_BINARY_DIR})
target_compile_definitions(test_pad_glyphs PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_pad_glyphs PRIVATE m pthread x2_runtime_config
                                              Freetype::Freetype)
add_test(NAME pad_glyphs COMMAND test_pad_glyphs)
add_test(NAME pad_glyphs_disabled COMMAND test_pad_glyphs --disabled)
set_tests_properties(pad_glyphs pad_glyphs_disabled PROPERTIES
                     ENVIRONMENT "X2_UI_RESOURCE_DIR=${CMAKE_BINARY_DIR}/ui")

# The glyph-loop detector, against both classes. On the real run it has only
# ever answered zero; this is where it is made to answer one.
add_executable(test_prompt_glyph_draw tests/test_prompt_glyph_draw.cpp
               src/native/prompt_glyph_draw.cpp src/native/prompt_string_census.cpp
               src/native/prompt_glyphs.cpp
               src/native/prompt_glyph_atlas.cpp
               src/native/prompt_glyph_metrics.cpp
               src/native/text_put.cpp
               src/native/prompt_glyph_quads.cpp
               src/native/keycap_run.cpp
               src/native/keycap_labels.cpp src/ui/ui_resources.cpp
               src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
add_dependencies(test_prompt_glyph_draw x2_pad_glyph_header
                 x2_prompt_glyph_atlas)
target_include_directories(test_prompt_glyph_draw
                           PRIVATE src/runtime/x86_abi src/native src/gen
                                   ${CMAKE_BINARY_DIR})
target_compile_definitions(test_prompt_glyph_draw PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_prompt_glyph_draw PRIVATE m pthread x2_runtime_config
                                                     Freetype::Freetype)
add_test(NAME prompt_glyph_draw COMMAND test_prompt_glyph_draw)
set_tests_properties(prompt_glyph_draw PROPERTIES
                     ENVIRONMENT "X2_UI_RESOURCE_DIR=${CMAKE_BINARY_DIR}/ui")

# The registry store: unbounded, with stable value addresses as it grows.
add_executable(test_advapi32_store tests/test_advapi32_store.cpp
               src/native/advapi32.cpp src/native/guest_memory.cpp
               src/native/guest_memory_arena.cpp tests/native_stubs.cpp)
target_include_directories(test_advapi32_store
                           PRIVATE src/runtime/x86_abi src/native src/config)
target_compile_definitions(test_advapi32_store PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_advapi32_store PRIVATE m pthread x2_runtime_config)
add_test(NAME advapi32_store COMMAND test_advapi32_store)

add_executable(test_prompt_glyph_metrics tests/test_prompt_glyph_metrics.cpp
               src/native/prompt_glyph_metrics.cpp
               src/native/text_put.cpp
               src/native/prompt_glyphs.cpp
               src/native/prompt_glyph_atlas.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
add_dependencies(test_prompt_glyph_metrics x2_pad_glyph_header
                 x2_prompt_glyph_atlas)
target_include_directories(test_prompt_glyph_metrics
                           PRIVATE src/runtime/x86_abi src/native src/gen
                                   ${CMAKE_BINARY_DIR})
target_compile_definitions(test_prompt_glyph_metrics PRIVATE _GNU_SOURCE
                                                           X86_NATIVE)
target_link_libraries(test_prompt_glyph_metrics PRIVATE m pthread)
add_test(NAME prompt_glyph_metrics COMMAND test_prompt_glyph_metrics)

add_executable(test_prompt_glyph_batch tests/test_prompt_glyph_batch.cpp
               src/native/prompt_glyph_batch.cpp src/native/prompt_glyph_quads.cpp
               tests/native_stubs.cpp)
target_include_directories(test_prompt_glyph_batch
                           PRIVATE src/runtime/x86_abi src/native src/gpu)
add_test(NAME prompt_glyph_batch COMMAND test_prompt_glyph_batch)

add_executable(test_ui_transform tests/test_ui_transform.cpp
               src/native/ui_transform.cpp src/gpu/gpu_matrix.cpp
               src/native/guest_memory.cpp src/native/guest_memory_arena.cpp tests/native_stubs.cpp)
target_include_directories(test_ui_transform
                           PRIVATE src/runtime/x86_abi src/native src/gpu)
target_compile_definitions(test_ui_transform PRIVATE X86_NATIVE)
target_link_libraries(test_ui_transform PRIVATE pthread)
add_test(NAME ui_transform COMMAND test_ui_transform)

add_executable(test_dialog_prompts tests/test_dialog_prompts.cpp
               src/native/dialog_prompts.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
target_include_directories(test_dialog_prompts PRIVATE src/runtime/x86_abi src/native
                                                       src/input)
target_compile_definitions(test_dialog_prompts PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_dialog_prompts PRIVATE pthread)
add_test(NAME dialog_prompts COMMAND test_dialog_prompts)

add_executable(test_xbox_defaults tests/test_xbox_defaults.cpp
               src/native/xbox_defaults.cpp)
target_include_directories(test_xbox_defaults PRIVATE src/native)
add_test(NAME xbox_defaults COMMAND test_xbox_defaults)


# The guest printf format walker. It builds crt.cpp against a stub runtime, so it
# needs no guest runtime and runs in the ordinary suite.
add_executable(test_vformat tests/test_vformat.cpp src/native/crt.cpp
               src/native/crt_format.cpp
               src/native/crt_console.cpp
               src/native/crt_stdio.cpp
               src/native/host_dir_cache.cpp
               src/native/guest_file_io.cpp
               src/native/crt_write_watch.cpp src/runtime/x86_abi/x87crt.cpp
            tests/native_stubs.cpp
               src/native/guest_heap.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_vformat PRIVATE src/runtime/x86_abi src/native)
target_compile_definitions(test_vformat PRIVATE _GNU_SOURCE X86_NATIVE)
target_link_libraries(test_vformat PRIVATE m pthread)
add_test(NAME vformat COMMAND test_vformat)

add_executable(test_x87crt tests/test_x87crt.cpp src/runtime/x86_abi/x87crt.cpp)

add_executable(test_platform_mman tests/test_platform_mman.cpp)
add_test(NAME platform_mman COMMAND test_platform_mman)

# The granule seam redirects the libc calls platform_mman.h makes inline on
# POSIX; Windows makes them out of line in x2_platform (test_platform_mman).
if(NOT WIN32)
    add_library(guest_memory_test_impl OBJECT src/native/guest_memory.cpp
                src/native/guest_memory_arena.cpp)
    target_compile_definitions(guest_memory_test_impl PRIVATE
        X2_GUEST_ARENA_RESERVED=1
        sysconf=x2_test_sysconf mmap=x2_test_mmap mprotect=x2_test_mprotect)
    add_executable(test_guest_memory tests/test_guest_memory.cpp
        $<TARGET_OBJECTS:guest_memory_test_impl>)
    target_include_directories(test_guest_memory PRIVATE src/native)
    target_link_libraries(test_guest_memory PRIVATE pthread)
    foreach(_granule IN ITEMS 4096 16384 -1 0 2048 12288)
        add_test(NAME guest_memory_${_granule}
                 COMMAND test_guest_memory ${_granule})
    endforeach()
endif()

add_executable(test_threads_ready tests/test_threads_ready.cpp
               src/native/threads_ready.cpp)
target_include_directories(test_threads_ready PRIVATE src/native)
add_test(NAME threads_ready COMMAND test_threads_ready)

add_executable(test_kernel32_wait tests/test_kernel32_wait.cpp
               src/native/kernel32_wait.cpp tests/native_stubs.cpp)
target_include_directories(test_kernel32_wait PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_kernel32_wait PRIVATE X86_NATIVE)
target_link_libraries(test_kernel32_wait PRIVATE lucent::lucent)
add_test(NAME kernel32_wait COMMAND test_kernel32_wait)

add_executable(test_module_name_memo tests/test_module_name_memo.cpp
               src/native/module_name_memo.cpp)
target_include_directories(test_module_name_memo PRIVATE src/native)
add_test(NAME module_name_memo COMMAND test_module_name_memo)

add_executable(test_guest_clock tests/test_guest_clock.cpp src/native/guest_clock.cpp)
target_include_directories(test_guest_clock PRIVATE src/native)
target_link_libraries(test_guest_clock PRIVATE lucent::lucent)
add_test(NAME guest_clock COMMAND test_guest_clock)

# The guest's command line, run twice: plain, and with the exact tokens that
# leaked onto the game's line in the browser. Both must yield the same line.
add_executable(test_guest_command_line tests/test_guest_command_line.cpp
               src/native/guest_command_line.cpp)
target_include_directories(test_guest_command_line PRIVATE src/native ${CMAKE_BINARY_DIR})
target_link_libraries(test_guest_command_line PRIVATE lucent::lucent)
add_test(NAME guest_command_line COMMAND test_guest_command_line)
add_test(NAME guest_command_line_ignores_host_argv COMMAND test_guest_command_line
         --appimage /some/install --set quantum=20000 --unbounded)

add_executable(test_crt_in_image_overrides tests/test_crt_in_image_overrides.cpp
               src/native/crt_in_image_overrides.cpp src/runtime/x86_abi/x87crt.cpp
               tests/native_stubs.cpp)
target_include_directories(test_crt_in_image_overrides PRIVATE src/native src/runtime/x86_abi)
target_link_libraries(test_crt_in_image_overrides PRIVATE m)
add_test(NAME crt_in_image_overrides COMMAND test_crt_in_image_overrides)

add_executable(test_frame_limiter_wait tests/test_frame_limiter_wait.cpp
               src/native/frame_limiter_wait.cpp)
target_include_directories(test_frame_limiter_wait PRIVATE src/native)
add_test(NAME frame_limiter_wait COMMAND test_frame_limiter_wait)

add_executable(test_audio_adpcm tests/test_audio_adpcm.cpp
               src/native/audio_adpcm.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
target_include_directories(test_audio_adpcm PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_audio_adpcm PRIVATE X86_NATIVE)
add_test(NAME audio_adpcm COMMAND test_audio_adpcm)

add_executable(test_vertex_color_swizzle tests/test_vertex_color_swizzle.cpp
               src/native/vertex_color_swizzle.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
target_include_directories(test_vertex_color_swizzle PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_vertex_color_swizzle PRIVATE X86_NATIVE)
add_test(NAME vertex_color_swizzle COMMAND test_vertex_color_swizzle)

add_executable(test_vertex_builder tests/test_vertex_builder.cpp
               src/native/vertex_builder.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
target_include_directories(test_vertex_builder PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_vertex_builder PRIVATE X86_NATIVE)
add_test(NAME vertex_builder COMMAND test_vertex_builder)

add_executable(test_override_leaf tests/test_override_leaf.cpp
               src/native/override_leaf.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_override_leaf PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_override_leaf PRIVATE X86_NATIVE)
target_link_libraries(test_override_leaf PRIVATE x86port_runtime lucent::lucent)
add_test(NAME override_leaf COMMAND test_override_leaf)

add_executable(test_x86_import_fastpath tests/test_x86_import_fastpath.cpp
               src/native/x86_import_fastpath.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               src/native/guest_clock.cpp src/runtime/x86_abi/x87crt.cpp tests/native_stubs.cpp)
target_include_directories(test_x86_import_fastpath PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_x86_import_fastpath PRIVATE TEST_SUITE=1 X86_NATIVE)
target_link_libraries(test_x86_import_fastpath PRIVATE x86port_runtime lucent::lucent)
add_test(NAME x86_import_fastpath COMMAND test_x86_import_fastpath)

add_executable(test_attr_stack tests/test_attr_stack.cpp
               src/native/attr_stack.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
target_include_directories(test_attr_stack PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_attr_stack PRIVATE TEST_SUITE=1 X86_NATIVE)
target_link_libraries(test_attr_stack PRIVATE lucent::lucent)
add_test(NAME attr_stack COMMAND test_attr_stack)

add_executable(test_guest_heap tests/test_guest_heap.cpp src/native/guest_heap.cpp
               src/native/guest_memory.cpp src/native/guest_memory_arena.cpp)
target_include_directories(test_guest_heap PRIVATE src/runtime/x86_abi src/native)
target_compile_definitions(test_guest_heap PRIVATE X86_NATIVE)
target_link_libraries(test_guest_heap PRIVATE pthread)
add_test(NAME guest_heap COMMAND test_guest_heap)

add_executable(test_box_cull tests/test_box_cull.cpp src/native/box_cull.cpp
               src/native/x87_exact.cpp)
target_include_directories(test_box_cull PRIVATE src/native)
target_link_libraries(test_box_cull PRIVATE x86port_runtime)
add_test(NAME box_cull COMMAND test_box_cull)

add_executable(test_ig_matrix tests/test_ig_matrix.cpp src/native/ig_matrix.cpp
               src/native/x87_exact.cpp)
target_include_directories(test_ig_matrix PRIVATE src/native)
target_link_libraries(test_ig_matrix PRIVATE x86port_runtime)
add_test(NAME ig_matrix COMMAND test_ig_matrix)

add_executable(test_ig_matrix_invert tests/test_ig_matrix_invert.cpp
               src/native/ig_matrix_invert.cpp)
target_include_directories(test_ig_matrix_invert PRIVATE src/native)
target_link_libraries(test_ig_matrix_invert PRIVATE x86port_runtime)
add_test(NAME ig_matrix_invert COMMAND test_ig_matrix_invert)

# The same x87-order overrides in the configuration the browser, ARM64 and
# Apple Silicon run, where x87_real is a double: on an x86 host a 64-bit long
# double builds exactly that from the shipping sources, under `double_` names
# so the test can hold it to the exact build beside it.
include(CheckCCompilerFlag)
check_c_compiler_flag(-mlong-double-64 X2_HAS_LONG_DOUBLE_64)
if(X2_HAS_LONG_DOUBLE_64)
    set(_x2_x87_math src/native/box_cull.cpp src/native/ig_matrix.cpp
                     src/native/ig_matrix_invert.cpp)
    add_library(x2_x87_double_math STATIC ${_x2_x87_math})
    target_include_directories(x2_x87_double_math PRIVATE src/native)
    target_compile_options(x2_x87_double_math PRIVATE -mlong-double-64)
    set(_x2_x87_math_names
        box_cull_extent box_cull_corners box_cull_corners_guest_order
        box_cull_corner_code box_cull_corner_code_extended box_cull_classify
        box_cull_guard_band box_cull_guard_band_status
        box_cull_bounded_verdict ig_matrix44_multiply ig_matrix44_invert)
    foreach(_x2_name IN LISTS _x2_x87_math_names)
        target_compile_definitions(x2_x87_double_math
                                   PRIVATE ${_x2_name}=double_${_x2_name})
    endforeach()
    target_link_libraries(x2_x87_double_math PRIVATE x86port_runtime)
    add_executable(test_x87_real_double tests/test_x87_real_double.cpp
                   ${_x2_x87_math})
    target_include_directories(test_x87_real_double PRIVATE src/native)
    target_link_libraries(test_x87_real_double PRIVATE x2_x87_double_math
                                                       x86port_runtime m)
    add_test(NAME x87_real_double COMMAND test_x87_real_double)
endif()

add_executable(test_skin tests/test_skin.cpp src/native/skin.cpp)
target_include_directories(test_skin PRIVATE src/native)
add_test(NAME skin COMMAND test_skin)

add_executable(test_audio_channel_poll tests/test_audio_channel_poll.cpp
               src/native/audio_channel_poll.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
               tests/native_stubs.cpp)
target_include_directories(test_audio_channel_poll PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_audio_channel_poll PRIVATE TEST_SUITE=1 X86_NATIVE)
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
    add_executable(test_x87host tests/test_x87host.cpp src/runtime/x86_abi/x87host.cpp)
    target_include_directories(test_x87host PRIVATE src/runtime/x86_abi)
    target_link_libraries(test_x87host PRIVATE m)
    add_test(NAME x87host COMMAND test_x87host)
endif()

add_executable(test_x86callbacks
    tests/test_x86callbacks.cpp src/runtime/x86_abi/x86callbacks.cpp
    src/native/guest_memory.cpp src/native/guest_memory_arena.cpp tests/native_stubs.cpp)
target_include_directories(test_x86callbacks PRIVATE src/runtime/x86_abi src/native)
target_compile_definitions(test_x86callbacks PRIVATE X86_NATIVE)
target_link_libraries(test_x86callbacks PRIVATE pthread)
add_test(NAME x86callbacks COMMAND test_x86callbacks)

# The native executable, not only run.sh, loads the project .env. These tests
# link the exact shipping parser and prove both precedence directions plus its
# refusal of malformed input and executable-project precedence.
add_executable(test_env_file tests/test_env_file.cpp src/native/env_file.cpp)
target_include_directories(test_env_file PRIVATE src/native src/config)
add_test(NAME env_file_load COMMAND test_env_file load)
add_test(NAME env_file_preserve COMMAND test_env_file preserve)
add_test(NAME env_file_malformed COMMAND test_env_file malformed)
add_test(NAME env_file_unknown_key COMMAND test_env_file unknown-key)
add_test(NAME env_file_executable_precedence
    COMMAND test_env_file executable-precedence)

add_executable(test_x2native_options
    tests/test_x2native_options.cpp src/native/x2native_options.cpp
    src/config/environment.cpp)
target_include_directories(test_x2native_options PRIVATE src/native)
add_test(NAME x2native_options COMMAND test_x2native_options)

add_executable(test_options_menu
    tests/test_options_menu.cpp src/native/options_menu.cpp
    src/native/guest_heap.cpp src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
    src/ui/settings_overlay_state.cpp
    tests/options_menu_stubs.cpp)
target_include_directories(test_options_menu PRIVATE src/runtime/x86_abi src/native src/ui)
target_compile_definitions(test_options_menu PRIVATE X86_NATIVE)
target_link_libraries(test_options_menu PRIVATE pthread)
add_test(NAME options_menu COMMAND test_options_menu)

add_executable(test_save_catalog
    tests/test_save_catalog.cpp src/save/save_catalog.cpp)
target_include_directories(test_save_catalog PRIVATE src/save)
add_test(NAME save_catalog COMMAND test_save_catalog)

add_executable(test_save_directory
    tests/test_save_directory.cpp src/save/save_directory.cpp)
target_include_directories(test_save_directory PRIVATE src/save src/native)
add_test(NAME save_directory COMMAND test_save_directory)

add_executable(test_autosave_policy
    tests/test_autosave_policy.cpp src/save/autosave_policy.cpp)
target_include_directories(test_autosave_policy PRIVATE src/save)
add_test(NAME autosave_policy COMMAND test_autosave_policy)

add_executable(test_continue_policy
    tests/test_continue_policy.cpp src/save/continue_policy.cpp)
target_include_directories(test_continue_policy PRIVATE src/save)
add_test(NAME continue_policy COMMAND test_continue_policy)

add_executable(test_autosave_storage
    tests/test_autosave_storage.cpp src/save/autosave_storage.cpp
    src/save/save_directory.cpp)
target_include_directories(test_autosave_storage PRIVATE src/save src/native)
add_test(NAME autosave_storage COMMAND test_autosave_storage)
set_tests_properties(autosave_storage PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_autosave_format
    tests/test_autosave_format.cpp src/save/autosave_format.cpp)
target_include_directories(test_autosave_format PRIVATE src/save)
add_test(NAME autosave_format COMMAND test_autosave_format)

add_executable(test_save_trace
    tests/test_save_trace.cpp src/save/save_trace.cpp)
target_include_directories(test_save_trace PRIVATE src/save)
add_test(NAME save_trace COMMAND test_save_trace)

add_executable(test_shadow_trace
    tests/test_shadow_trace.cpp tools/proxy_d3d8/shadow_trace.c)
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

add_test(NAME hud_geometry COMMAND ${Python3_EXECUTABLE} -m unittest
         tests.test_hud_geometry)
set_tests_properties(hud_geometry PROPERTIES
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
    add_executable(test_virtual_pad tests/test_virtual_pad.cpp)
    target_link_libraries(test_virtual_pad PRIVATE SDL3::SDL3)
    add_test(NAME virtual_pad COMMAND test_virtual_pad)
    # A machine with no gamepad subsystem SKIPs (77) rather than failing, and
    # rather than passing having checked nothing.
    set_tests_properties(virtual_pad PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

add_executable(test_control_png tests/test_control_png.cpp src/native/control_png.cpp)
target_include_directories(test_control_png PRIVATE src/native)
target_compile_definitions(test_control_png PRIVATE
    CHECK_PNG="${CMAKE_CURRENT_SOURCE_DIR}/tools/check_png.py"
    CHECK_PNG_PYTHON="${Python3_EXECUTABLE}")
add_test(NAME control_png COMMAND test_control_png)

add_executable(test_control_screenshot
    tests/test_control_screenshot.cpp
    src/native/control_screenshot.cpp
    src/native/control_png.cpp)
target_include_directories(test_control_screenshot PRIVATE src/native src/gpu)
add_test(NAME control_screenshot COMMAND test_control_screenshot)
add_test(NAME pad_glyph_manifest
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py --selftest)

# The atlas header bootstrap.py publishes at every provision: this proves
# every published codepoint is actually inked -- an all-blank atlas is exactly
# the uniform output an unvalidated rasteriser produces.
add_test(NAME prompt_glyph_atlas
         COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/render_prompt_glyphs.py --selftest
                 --cxx ${CMAKE_CXX_COMPILER})
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

# The browser touch driver reads the product's own touch census out of the
# console, so its reader is checked against the text the product prints --
# test_touch_runtime's own report -- and not only against fixtures.
add_test(NAME web_touch_play
         COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_web_touch_play)
set_tests_properties(web_touch_play PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "X2_TEST_TOUCH_RUNTIME_BINARY=$<TARGET_FILE:test_touch_runtime>")

# The Zen driver's frame-time reader, against the heartbeat's window line.
add_test(NAME zen_play
         COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_zen_play)
set_tests_properties(zen_play PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Every browser recorder reads the console through cdp_console; a worker that
# starts after it attaches, and the stack a trap carries, must both reach it.
add_test(NAME cdp_console
         COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_cdp_console)
set_tests_properties(cdp_console PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


# The 42 binding-row storage keys and display labels in
# src/input/binding_rows.cpp are read out of XMen2.exe and igct.bnx; this diffs
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
add_test(NAME clang_format
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_format.py)
add_test(NAME clang_format_selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_format.py --selftest)
set_tests_properties(clang_format clang_format_selftest PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME clang_tidy
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/clang_tidy.py
                 --build-dir ${CMAKE_BINARY_DIR} --jobs 2)
set_tests_properties(clang_tidy PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME jit_map_profile
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/jit_map_profile.py --selftest)
set_tests_properties(jit_map_profile PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
        # The same rule that says which module is the game's executable also
        # names it, so the guest's own command line has one authority rather
        # than a second hardcoded "XMen2.exe" that could drift from this list.
        set(X2_INSTALL_MAIN_IMAGE "${m}.exe")
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
    # Maintained fork adds SDL_GPU WGSL resources; native formats retain the
    # upstream f9b8c9e2935d5df2c7dff2c190d3968e99b0c3dc contract.
    URL https://github.com/SomeoneIsWorking/RmlUi/archive/b313924185871a1d4f2fddbdedc1b63ef3e07cf7.tar.gz
    URL_HASH SHA256=6dc1de88578fe237dce4c34791ed58f995d47b2e78d1c5b7416cb50f007feac3
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
if(NOT ANDROID AND NOT EMSCRIPTEN)
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
# The touch buttons' own art, authored as SVG and rasterized by SDL_image at
# load time. tools/ui_resource_files.py lists the same files for packaging.
foreach(x2_touch_icon punch smash use jump menu)
    configure_file("${CMAKE_SOURCE_DIR}/assets/ui/touch_${x2_touch_icon}.svg"
                   "${CMAKE_BINARY_DIR}/ui/touch_${x2_touch_icon}.svg" COPYONLY)
endforeach()
foreach(x2_assign_mark on off)
    configure_file("${CMAKE_SOURCE_DIR}/assets/ui/assign_${x2_assign_mark}.svg"
                   "${CMAKE_BINARY_DIR}/ui/assign_${x2_assign_mark}.svg" COPYONLY)
endforeach()
add_library(x2_rmlui_ui STATIC
    src/ui/controller_assignment_rows.cpp
    src/ui/rmlui_ui.cpp
    src/ui/ui_resources.cpp
    src/ui/settings_document.cpp
    src/ui/display_settings_document.cpp
    src/ui/hud_settings_document.cpp
    src/ui/keyboard_bindings_document.cpp
    src/ui/rml_text.cpp
    src/ui/touch_document.cpp
    src/ui/skip_document.cpp
    src/ui/igb_textures.cpp
    src/ui/settings_overlay_state.cpp)
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
                                        PRIVATE lucent::lucent alchemy)

include(cmake/Shaders.cmake)

set(X2_NATIVE_SOURCES
    src/native/x2native.cpp
    src/native/fault_report.cpp
    src/native/engine_file_path.cpp
    src/native/guest_memory.cpp src/native/guest_memory_arena.cpp
    src/native/guest_memory_probe.cpp
    src/native/x2native_options.cpp
    src/native/sdl_host_setup.cpp
    src/native/install_picker.cpp
    src/native/install_archive.cpp
    src/native/install_validation.cpp
    src/native/pe_header.cpp
    src/native/android_bridge.cpp
    src/input/alchemy_controller_bridge.cpp
    src/input/directinput_controller_sample.cpp
    src/input/ig_controller_manager_adapter.cpp
    src/config/directory_create.cpp
    src/config/settings.cpp src/config/hud_settings.cpp
    src/config/input_assignments.cpp
    src/config/settings_store.cpp
    src/config/boot_mode.cpp
    src/native/boot_mode_policy.cpp
    src/native/boot_mode_runtime.cpp
    src/native/boot_menu_transition.cpp
    src/native/boot_splash_policy.cpp
    src/native/x86_engine.cpp
    src/native/x86_engine_diagnostic.cpp
    src/native/x86_engine_jit_pool.cpp
    src/native/override_leaf.cpp
    src/native/x86_engine_report.cpp
    src/native/x86_engine_dispatch_report.cpp
    src/native/guest_command_line.cpp
    src/native/guest_modules.cpp
    src/native/host_imports_advapi32.cpp
    src/native/host_imports_all.cpp
    src/native/host_imports_dinput.cpp
    src/native/host_imports_dinput8.cpp
    src/native/host_imports_d3d8.cpp
    src/native/host_imports_gdi32.cpp
    src/native/host_imports_kernel32.cpp
    src/native/host_imports_msvcr71.cpp
    src/native/host_imports_msvcrt.cpp
    src/native/host_imports_ole32.cpp
    src/native/host_imports_user32.cpp
    src/native/host_imports_winmm.cpp
    src/native/host_imports_ws2_32.cpp
    src/native/host_imports.cpp
    src/native/guest_body.cpp
    src/native/module_name_memo.cpp
    src/native/x86_engine_selftest.cpp
    src/native/x86_engine_jump_selftest.cpp
    src/native/x86_dispatch.cpp
    src/native/x86_diag_dump.cpp
    src/native/x86_guest_call_stack.cpp
    src/native/x86_engine_intercept.cpp
    src/native/x86_engine_dispatch.cpp
    src/native/x86_import_fastpath.cpp
    src/native/attr_stack.cpp
    src/native/attr_stack_verify.cpp
    src/native/box_cull.cpp
    src/native/box_cull_override.cpp
    src/native/x87_exact.cpp
    src/native/skin.cpp
    src/native/skin_override.cpp
    src/native/ig_matrix.cpp
    src/native/ig_matrix_override.cpp
    src/native/ig_matrix_invert.cpp
    src/native/ig_matrix_invert_override.cpp
    src/native/x86_engine_jit_diag.cpp
    src/native/guest_inspect.cpp
    src/native/x86_engine_x87_census.cpp
    src/native/x86_engine_x87_precision.cpp
    src/native/x86_dispatch_report.cpp
    src/presentation/aspect_fit.cpp
    src/presentation/boot_blackout.cpp
    src/presentation/window_settings.cpp
    src/presentation/display_mode_seed.cpp
    src/presentation/live_resolution.cpp
    src/presentation/resolution_ladder.cpp
    src/presentation/display_geometry.cpp
    src/native/display_mode_runtime.cpp
    src/native/ui_text_scale.cpp
    src/native/dialog_selection_scale.cpp
    src/native/dialog_selection_scale_policy.cpp
    src/native/text_caller_probe.cpp
    src/native/prompt_glyph_draw.cpp
    src/native/prompt_string_census.cpp
    src/native/prompt_glyph_atlas.cpp
    src/native/prompt_glyph_batch.cpp
    src/native/prompt_glyph_metrics.cpp
    src/native/prompt_glyph_quads.cpp
    src/native/keycap_run.cpp
    src/native/keycap_labels.cpp
    src/native/prompt_tokens.cpp
    src/native/ui_transform.cpp
    src/input/binding_rows.cpp
    src/input/controller_hotplug.cpp
    src/input/controller_instance.cpp
    src/input/input_probe_lifecycle.cpp
    src/input/joystick_neutral.cpp
    src/input/player_input.cpp
    src/input/player_participation_policy.cpp
    src/input/transient_controller_assignment.cpp
    src/input/input_record.cpp
    src/save/save_catalog.cpp
    src/save/save_directory.cpp
    src/save/autosave_policy.cpp
    src/save/autosave_storage.cpp
    src/save/autosave_format.cpp
    src/save/continue_policy.cpp
    src/save/save_trace.cpp
    src/save/load_game_menu_policy.cpp
    src/native/autosave_runtime.cpp
    src/native/boot_player_selection.cpp
    src/native/continue_runtime.cpp
    src/native/exact_save_load.cpp
    src/native/load_game_menu_runtime.cpp
    src/native/save_trace_runtime.cpp
    src/native/pe_map.cpp
    src/native/pe_export_search.cpp
    src/native/platform_file_map.cpp
    src/native/win32_sdl.cpp
    src/native/win32_mouse.cpp
    src/native/win32_pointer.cpp
    src/native/win32_events.cpp
    src/native/guest_heap.cpp
    src/native/guest_file_io.cpp
    src/native/crt_console.cpp
    src/native/crt_stdio.cpp
    src/native/crt.cpp
    src/native/crt_format.cpp
    src/native/crt_aliases.cpp
    src/native/crt_setjmp.cpp
    src/native/crt_write_watch.cpp
    src/native/crt_selftest.cpp
    src/native/crt_in_image_overrides.cpp
    src/native/audio_adpcm.cpp
    src/native/audio_adpcm_verify.cpp
    src/native/vertex_color_swizzle.cpp
    src/native/vertex_color_swizzle_verify.cpp
    src/native/vertex_builder.cpp
    src/native/vertex_builder_verify.cpp
    src/native/audio_channel_poll.cpp
    src/native/audio_channel_poll_verify.cpp
    src/runtime/x86_abi/x87crt.cpp
    src/native/kernel32.cpp
    src/native/kernel32_wait.cpp
    src/native/kernel32_virtual.cpp
    src/native/win_path.cpp
    src/native/host_dir_cache.cpp
    src/native/advapi32.cpp
    src/native/advapi32_host_store.cpp
    src/native/fault_selftest.cpp
    src/native/ole32.cpp
    src/native/gdi32.cpp
    src/native/dinput.cpp
    src/native/dinput8.cpp
    src/native/dinput8_controller_slots.cpp
    src/native/dinput8_hotplug.cpp
    src/native/dinput_device.cpp
    src/native/dinput_device_registry.cpp
    src/native/dinput_device_state.cpp
    src/native/dinput_system.cpp
    src/native/dinput_fifo.cpp
    src/native/dinput_script.cpp
    src/native/dinput_joystick.cpp
    src/native/dinput_pad.cpp
    src/native/dinput_pad_subsystem.cpp
    src/native/dinput_pad_identity.cpp
    src/native/dinput_pad_sample.cpp
    src/native/dinput_pad_virtual.cpp
    src/native/dinput_pad_virtual_actions.cpp
    src/native/dinput_pad_virtual_device.cpp
    src/native/dinput_pad_virtual_reader.cpp
    src/native/dialog_prompts.cpp
    src/native/pad_glyphs.cpp
    src/native/prompt_glyphs.cpp
    src/native/prompt_labels.cpp
    src/native/input_binding_sets.cpp
    src/native/input_bindings.cpp
    src/native/player_participation.cpp
    src/native/player_participation_probe.cpp
    src/native/cutscene_skip_publication.cpp
    src/native/cutscene_skip_probe.cpp
    src/native/behaved_context.cpp
    src/native/behaved_player.cpp
    src/native/cutscene_event_player.cpp
    src/native/conversation_player.cpp
    src/native/cutscene_dialogue.cpp
    src/native/cutscene_script_audio.cpp
    src/native/cutscene_player_policy.cpp
    src/native/cutscene_control_clock.cpp
    src/native/cutscene_player.cpp
    src/native/stick_axis_override.cpp
    src/native/input_probe.cpp
    src/native/text_put.cpp
    src/input/touch_controls.cpp
    src/input/thumb_stick.cpp
    src/input/touch_runtime.cpp
    src/input/touch_skip_button.cpp
    src/input/cutscene_skip.cpp
    src/input/touch_pad_publisher.cpp
    src/input/touch_menu_controls.cpp
    src/input/touch_pointer.cpp
    src/input/touch_visuals.cpp
    src/input/touch_inject.cpp
    src/input/touch_census.cpp
    src/input/touch_pad.cpp
    src/input/touch_source.cpp
    src/presentation/touch_layout.cpp
    src/input/gameplay_control.cpp
    src/native/touch_hud_runtime.cpp
    src/native/hud_draw_runtime.cpp
    src/native/power_slots_runtime.cpp
    src/native/guest_teb.cpp
    src/native/hud_portrait_position.cpp
    src/native/hud_portrait_position_runtime.cpp
    src/presentation/hud_layout.cpp
    src/native/xbox_defaults.cpp
    src/native/dsound.cpp
    src/native/dsound_mixer.cpp
    src/audio/movie_audio.cpp
    src/media/fmv_audio_decode.cpp
    src/media/fmv_decoder_drain.cpp
    src/media/fmv_player.cpp
    src/media/fmv_policy.cpp
    src/media/fmv_probe.cpp
    src/media/fmv_sfd.cpp
    src/media/fmv_timing.cpp
    src/native/guest_clock.cpp
    src/native/control.cpp
    src/native/control_http.cpp
    src/native/control_save_route.cpp
    src/native/control_console.cpp
    src/native/retail_front_end.cpp
    src/native/campaign_snapshot.cpp
    src/native/lan_session.cpp
    src/native/lan_session_director.cpp
    src/native/lan_coordinator.cpp
    src/native/retail_net_session.cpp
    src/net/lan_presence.cpp
    src/net/lan_presence_policy.cpp
    src/net/lan_presence_protocol.cpp
    src/net/lan_rendezvous.cpp
    src/net/udp_broadcast_socket.cpp
    src/native/lan_nat_negotiation.cpp
    src/native/storage_device_check.cpp
    src/native/control_ui_route.cpp
    src/native/control_input_route.cpp
    src/native/macos_bundle.cpp
    src/native/control_performance_route.cpp
    src/native/control_status_route.cpp
    src/native/control_png.cpp
    src/native/control_query.cpp
    src/native/control_screenshot.cpp
    src/native/control_status.cpp
    src/native/json_string.cpp
    src/native/live_session.cpp
    src/native/ws2_32.cpp
    src/native/ws2_32_names.cpp
    src/native/winsock_host.cpp
    src/native/winsock_resolve.cpp
    src/native/lan_login.cpp
    src/native/shell32.cpp
    src/native/winmm.cpp
    src/native/thread_report.cpp
    src/native/threads.cpp
    src/native/threads_quantum.cpp
    src/native/threads_ready.cpp
    src/native/threads_engine_report.cpp
    src/native/threads_yield.cpp
    src/native/threads_memory.cpp
    src/native/x86_hotep.cpp
    src/native/x86_thunk_probe.cpp
    src/native/run_reports.cpp
    src/native/guest_layout_report.cpp
    src/native/x86rt_native.cpp
    src/native/env_file.cpp
    src/native/startup.cpp
    src/native/timer_accessor.cpp
    src/native/frame_limiter_wait.cpp
    src/native/movie.cpp
    src/native/movie_image_layout.cpp
    src/native/options_menu.cpp
    src/native/reportbox.cpp
    src/native/security.cpp
    src/native/conversation.cpp
    src/native/script_trace.cpp
    src/native/entity_spawn_probe.cpp
    src/native/heartbeat.cpp
    src/native/heartbeat_reports.cpp
    src/native/heartbeat_stall.cpp
    src/native/host_touch.cpp
    src/native/dinput_pad_report.cpp
    src/d3d8/d3d8_com.cpp
    src/d3d8/d3d8_caps.cpp
    src/d3d8/d3d8_state.cpp
    src/d3d8/d3d8_stateblock.cpp
    src/d3d8/d3d8_d3d8.cpp
    src/d3d8/d3d8_device.cpp
    src/d3d8/d3d8_leaf_methods.cpp
    src/d3d8/d3d8_device_lights.cpp
    src/d3d8/d3d8_live_resolution.cpp
    src/d3d8/d3d8_selector_probe.cpp
    src/d3d8/d3d8_selector_probe_json.cpp
    src/d3d8/d3d8_selector_probe_bridge.cpp
    src/d3d8/d3d8_texture_provenance.cpp
    src/d3d8/d3d8_lightlog.cpp
    src/d3d8/d3d8_surface.cpp
    src/d3d8/d3d8_resource.cpp
    src/d3d8/d3d8_texture_luma.cpp
    src/d3d8/d3d8_texture_stage.cpp
    src/d3d8/d3d8_vertex_layout.cpp
    src/d3d8/d3d8_frame_material.cpp
    src/d3d8/d3d8_drawcall.cpp
    src/d3d8/d3d8_draw_range.cpp
    src/d3d8/d3d8_lighting.cpp
    src/d3d8/d3d8_light_dump.cpp
    src/d3d8/d3d8_light_survey.cpp
    src/d3d8/d3d8_vertex_shader.cpp
    src/d3d8/d3d8_vs_decode.cpp
    src/d3d8/d3d8_vs_execute.cpp
    src/d3d8/d3d8_vs_gpu.cpp
    src/d3d8/d3d8_vs_draw.cpp
    src/d3d8/d3d8_screen_space_test.cpp
    src/d3d8/d3d8_device_bindings.cpp
    src/d3d8/d3d8_binding_selftest.cpp
    src/d3d8/d3d8_selftest_call.cpp
    src/d3d8/d3d8_gamma_selftest.cpp
    src/d3d8/d3d8_vs_gpu_selftest.cpp
    src/d3d8/d3d8_light_selftest.cpp
    src/d3d8/d3d8_host_report.cpp
    src/d3d8/d3d8_report.cpp
    src/vulkan/igvk_ark.cpp
    src/vulkan/igvk_probe.cpp
    src/vulkan/igvk_context.cpp
    src/gpu/gpu_depth.cpp
    src/gpu/gpu_device.cpp
    src/gpu/gpu_capture.cpp
    src/gpu/gpu_present_luma.cpp
    src/gpu/gpu_readback.cpp
    src/gpu/gpu_draw.cpp
    src/gpu/gpu_vertex_uniforms.cpp
    src/gpu/gpu_vs_program.cpp
    src/gpu/gpu_frame_epoch.cpp
    src/gpu/gpu_index_arena.cpp
    src/gpu/gpu_index_storage.cpp
    src/gpu/gpu_offscreen.cpp
    src/gpu/gpu_pipeline.cpp
    src/gpu/gpu_texture_format.cpp
    src/gpu/gpu_draw_trace.cpp
    src/gpu/gpu_frame_timing.cpp
    src/gpu/gpu_frame_timing_report.cpp
    src/gpu/gpu_host_timer.cpp
    src/gpu/gpu_shadow.cpp
    src/gpu/gpu_depth_binding.cpp
    src/gpu/gpu_shadow_selftest.cpp
    src/gpu/shadow_policy.cpp
    src/gpu/gpu_frame_submit.cpp
    src/gpu/gpu_present.cpp
    src/gpu/gpu_present_selftest.cpp
    src/gpu/gpu_prompt_glyphs.cpp
    src/gpu/gpu_host_selftest.cpp
    src/gpu/gpu_multistage_selftest.cpp
    src/gpu/gpu_matrix.cpp
    src/gpu/gpu_frame_init_selftest.cpp
    src/gpu/gpu_staging_ring.cpp
    src/gpu/gpu_upload_batch.cpp
    src/gpu/gpu_pass_binds.cpp
    src/gpu/gpu_pass_attachments.cpp
    src/gpu/gpu_upload_selftest.cpp
    src/gpu/gpu_headless.cpp
    src/gpu/gpu_device_selftest.cpp
    src/gpu/gpu_selftest.cpp
    src/gpu/gpu_lit_mvp_selftest.cpp
    src/gpu/gpu_frame_draw_selftest.cpp
    src/gpu/gpu_texture_selftest.cpp
    src/vulkan/igvk_slots_lifecycle.cpp
    src/vulkan/igvk_slots_frame.cpp
)
if(WIN32)
    list(APPEND X2_NATIVE_SOURCES src/native/fault_signals_win32.cpp)
else()
    list(APPEND X2_NATIVE_SOURCES src/native/fault_signals_posix.cpp)
endif()
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
add_subdirectory("${X2_X86PORT_DIR}" "${CMAKE_BINARY_DIR}/x86port"
                 EXCLUDE_FROM_ALL)
if(NOT TARGET x86port_runtime)
    message(FATAL_ERROR
        "x2native: shared/x86port does not provide the required "
        "product x86port_runtime target. The retired static product "
        "has been removed and is not a fallback; update shared/x86port.")
endif()

add_executable(test_guest_memory_window tests/test_guest_memory_window.cpp
    src/native/guest_memory.cpp src/native/guest_memory_arena.cpp src/native/guest_memory_probe.cpp
    src/native/guest_file_io.cpp)
target_include_directories(test_guest_memory_window PRIVATE
    src/native src/runtime/x86_abi)
target_compile_definitions(test_guest_memory_window PRIVATE
    X2_GUEST_ARENA_WINDOW=1 X86_NATIVE=1)
target_link_libraries(test_guest_memory_window PRIVATE x86port_runtime Threads::Threads)
if(WIN32)
    target_link_libraries(test_guest_memory_window PRIVATE x2_platform)
endif()
add_test(NAME guest_memory_window COMMAND test_guest_memory_window)

# guest_teb over the real guest arena; the heap and the PE TLS directory are
# the test's injected boundaries.
add_executable(test_pe_export_search tests/test_pe_export_search.cpp
               src/native/pe_export_search.cpp)
target_include_directories(test_pe_export_search PRIVATE src/native)
add_test(NAME pe_export_search COMMAND test_pe_export_search)

add_executable(test_guest_teb tests/test_guest_teb.cpp src/native/guest_teb.cpp
    src/native/guest_memory.cpp src/native/guest_memory_arena.cpp src/native/guest_memory_probe.cpp)
target_include_directories(test_guest_teb PRIVATE src/native src/runtime/x86_abi)
target_compile_definitions(test_guest_teb PRIVATE
    X2_GUEST_ARENA_WINDOW=1 X86_NATIVE=1)
target_link_libraries(test_guest_teb PRIVATE x86port_runtime Threads::Threads)
if(WIN32)
    target_link_libraries(test_guest_teb PRIVATE x2_platform)
endif()
add_test(NAME guest_teb COMMAND test_guest_teb)

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
                                            src/input src/save src/audio src/net
                                            src/media src/gpu src/d3d8 src/gen
                                            ${CMAKE_BINARY_DIR})
target_link_libraries(x2native PRIVATE x2_rmlui_ui lucent::lucent lucent::zip)
target_link_libraries(x2native PRIVATE Freetype::Freetype)
target_link_libraries(x2native PRIVATE x86port_runtime)
target_link_libraries(x2native PRIVATE alchemy::input)

# Native overrides are declared in C where they belong. Every direct or
# indirect guest call reaches the runtime dispatcher's override slot; no
# generated wiring, wrappers, or isolated guest source bodies exist.


target_compile_definitions(x2native PRIVATE X2_WITH_SDL _GNU_SOURCE)
target_compile_definitions(x2native PRIVATE X86_NATIVE)
# libm: the exe's x87 translations use atan2l/fmodl and friends.
# -pthread: the heartbeat (src/native/heartbeat.cpp) runs on its own thread
# so that a guest stuck anywhere still gets a liveness line.
target_link_libraries(x2native PRIVATE ${X2_SDL} ${X2_FFMPEG_TARGET}
                                       x2_runtime_config m Threads::Threads)
if(EMSCRIPTEN)
    include(cmake/WebTarget.cmake)
endif()
if(ANDROID)
    set_target_properties(x2native PROPERTIES OUTPUT_NAME main)
    target_link_options(x2native PRIVATE "-Wl,-Bsymbolic-functions")
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
        "x2.androidFrameworkJavaDir=${X2_ANDROID_FRAMEWORK_JAVA_DIR}\n"
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

# Every ctest process logs into the build tree: selftest runs must not rotate
# the player's run logs out of the user-data directory.
get_property(x2_all_tests DIRECTORY PROPERTY TESTS)
foreach(x2_test IN LISTS x2_all_tests)
  set_property(TEST ${x2_test} APPEND PROPERTY ENVIRONMENT
               "X2_LOG_DIR=${CMAKE_BINARY_DIR}/test-run-logs")
endforeach()
