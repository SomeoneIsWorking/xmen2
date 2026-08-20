cmake_minimum_required(VERSION 3.16)
project(xmen2_sdl LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3, for its GPU abstraction: it is the layer that will stand in for D3D8,
# and it maps onto Metal on macOS rather than needing MoltenVK underneath.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    find_package(PkgConfig REQUIRED)
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
# dependency on it at all -- only tools/make_pad_font.py reaches for its xmlb
# module, through the one resolver in tools/alchemy_path.py.

enable_testing()

add_test(NAME structure
         COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_structure.py)
add_test(NAME structure_selftest
         COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_structure.py --selftest)

add_executable(test_settings tests/test_settings.c src/config/settings.c)
target_include_directories(test_settings PRIVATE src/config)
target_compile_definitions(test_settings PRIVATE
    X2_TEST_SETTINGS_PATH="${CMAKE_BINARY_DIR}/test-x2native.conf")
add_test(NAME settings COMMAND test_settings)

add_executable(test_player_input tests/test_player_input.c
               src/input/player_input.c src/config/settings.c)
target_include_directories(test_player_input PRIVATE src/recomp src/native
                                                   src/config src/input)
add_test(NAME player_input COMMAND test_player_input)

set(X2_PAD_GLYPH_HEADER "${CMAKE_BINARY_DIR}/pad_glyph_codes.h")
add_custom_command(
    OUTPUT "${X2_PAD_GLYPH_HEADER}"
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            --emit-header "${X2_PAD_GLYPH_HEADER}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py"
            "${CMAKE_SOURCE_DIR}/assets/buttons/glyphs.json"
    COMMENT "x2native: generating button-glyph codepoints")
add_custom_target(x2_pad_glyph_header DEPENDS "${X2_PAD_GLYPH_HEADER}")


# The gamepad inventory, against an SDL virtual pad. dinput_pad.c depends on
# nothing in the guest runtime -- it is SDL on one side and plain values on the
# other -- so it links straight into a test binary.
add_executable(test_dinput_pad tests/test_dinput_pad.c src/native/dinput_pad.c
               src/native/guest_clock.c)
target_include_directories(test_dinput_pad PRIVATE src/native)
target_compile_definitions(test_dinput_pad PRIVATE X2_WITH_SDL)
target_link_libraries(test_dinput_pad PRIVATE ${X2_SDL})
add_test(NAME dinput_pad COMMAND test_dinput_pad)

add_executable(test_pad_glyphs tests/test_pad_glyphs.c
               src/native/pad_glyphs.c src/native/prompt_glyph_pack.c
               src/native/prompt_labels.c src/native/input_bindings.c
               tests/native_stubs.c)
add_dependencies(test_pad_glyphs x2_pad_glyph_header)
target_include_directories(test_pad_glyphs PRIVATE src/recomp src/native
                                                  ${CMAKE_BINARY_DIR})
target_compile_definitions(test_pad_glyphs PRIVATE _GNU_SOURCE)
add_test(NAME pad_glyphs COMMAND test_pad_glyphs)
add_test(NAME pad_glyphs_disabled COMMAND test_pad_glyphs --disabled)

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
               src/native/guest_heap.c)
target_include_directories(test_vformat PRIVATE src/recomp src/native)
target_compile_definitions(test_vformat PRIVATE _GNU_SOURCE)
target_link_libraries(test_vformat PRIVATE m)
add_test(NAME vformat COMMAND test_vformat)

add_executable(test_x87crt tests/test_x87crt.c src/recomp/x87crt.c)
target_include_directories(test_x87crt PRIVATE src/recomp)
target_link_libraries(test_x87crt PRIVATE m)
add_test(NAME x87crt COMMAND test_x87crt)

add_executable(test_x87host tests/test_x87host.c src/recomp/x87host.c)
target_include_directories(test_x87host PRIVATE src/recomp)
target_link_libraries(test_x87host PRIVATE m)
add_test(NAME x87host COMMAND test_x87host)

add_executable(test_x86callbacks
    tests/test_x86callbacks.c src/recomp/x86callbacks.c tests/native_stubs.c)
target_include_directories(test_x86callbacks PRIVATE src/recomp src/native)
add_test(NAME x86callbacks COMMAND test_x86callbacks)

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
add_test(NAME overrides COMMAND python3
         ${CMAKE_SOURCE_DIR}/tools/recomp_overrides.py --selftest)

# The guest stack-effect check (tools/stackcheck.py over an X2_STACKCHECK
# record). A guest call must return with esp raised by 4 plus the callee's RET
# immediate; nothing checked that before, and a single dword of drift inside a
# call tree surfaces as memory corruption somewhere unrelated -- FUN_0046b750's
# /GS epilogue read a slot four bytes below the cookie it had stored and
# reported a buffer overrun that never happened. The selftest is what makes the
# tool's usual answer ("every checked call is balanced", over a million calls)
# worth anything: it shows the checker flagging a 4-byte drift and excluding a
# tail-calling body, neither of which a normal run exercises.
add_test(NAME stackcheck COMMAND python3
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
add_test(NAME probes_generator COMMAND python3
         ${CMAKE_SOURCE_DIR}/tools/gen_probes.py --selftest)
add_test(NAME probes_current COMMAND python3
         ${CMAKE_SOURCE_DIR}/tools/gen_probes.py --check)
add_test(NAME oraclediff COMMAND python3
         ${CMAKE_SOURCE_DIR}/tools/oraclediff.py --selftest)

# The staleness gate's own discriminator: it must read a current stamp as
# current, a wrong one as stale, an ABSENT one as stale, and the fingerprint
# must move when the translator is edited. A checker only ever run against the
# good case is not a checker.
add_test(NAME emitted_stamp COMMAND python3
         ${CMAKE_SOURCE_DIR}/tools/check_emitted.py --selftest)


# The host D3D8 interface tables, against a real d3d8.h and against the game's
# own call sites. Runs the checker's SELF-TEST, not the check: the check needs
# scratch/recomp/libIGGfx.json, which is generated and gitignored, while the
# self-test proves the checks can fail and is meaningful with or without it.
add_test(NAME d3d8_abi
         COMMAND ${CMAKE_COMMAND} -E env python3
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

add_test(NAME pad_font
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/make_pad_font.py --selftest)

add_test(NAME pad_glyph_manifest
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/pad_glyph_manifest.py --selftest)

add_test(NAME prepare_native_assets
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/prepare_native_assets.py --selftest)


# The 42 binding-row names src/native/input_bindings.c ships are read out of
# XMen2.exe; this diffs the shipped table against the exe rather than trusting
# that they still agree. SKIPs loudly (77) without an install, because a check
# that had nothing to read must not look like a check that passed.
add_test(NAME binding_rows
         COMMAND ${CMAKE_COMMAND} -E env python3
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
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/lint.py)
set_tests_properties(python_lint PROPERTIES SKIP_RETURN_CODE 77
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME script_commands
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/script_commands.py --selftest)
set_tests_properties(script_commands PROPERTIES SKIP_RETURN_CODE 77
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME extract_fb
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/extract_fb.py --selftest)

# The oracle cache, proved to MISS as well as hit -- with a stub runner in
# place of Wine, because what is under test is the keying, not the game. A
# cache that has only ever been shown to hit is a cache that will serve a frame
# captured from a binary that has since been rebuilt.
add_test(NAME oracle_cache
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/oracle.py --selftest)


# ---------------------------------------------------------------------------
# x2native -- recompiled game code as a NATIVE binary (rc-native)
#
# The first artefact that is not hosted by Wine. It needs the generated
# recompiler output, which is gitignored, so the target only exists once
# `tools/recomp.py emit` and `tools/recomp.py native` have been run:
#
#   python3 tools/recomp.py emit   scratch/recomp/libIGDisplay.json \
#                                  src/recomp/libIGDisplay.c
#   python3 tools/recomp.py native scratch/recomp/libIGDisplay.json \
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
        src/ui/rmlui_ui.cpp
        src/ui/settings_document.cpp)
    target_include_directories(x2_rmlui_ui PRIVATE src/native src/config
                                                   src/presentation)
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
    find_program(X2_GLSLC NAMES glslc glslangValidator)
    if(NOT X2_GLSLC)
        message(FATAL_ERROR
            "x2native: no glslc (or glslangValidator) found, so the renderer's "
            "shaders cannot be built. Install shaderc/glslang. Refusing rather "
            "than building a binary that would run and draw nothing.")
    endif()
    set(X2_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${X2_SHADER_DIR})
    set(X2_SHADER_HEADERS "")
    foreach(st vert frag)
        set(_src ${CMAKE_SOURCE_DIR}/src/gpu/shaders/d3d8_fixed.${st})
        set(_out ${X2_SHADER_DIR}/d3d8_fixed_${st}.inc)
        # glslc -mfmt=c emits exactly a braced initialiser, so the .inc is
        # #included as one rather than assembled by echo and cat -- which is
        # shell redirection inside a custom command, and it silently produced a
        # malformed header for one of the two stages.
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${X2_GLSLC} -fshader-stage=${st} ${_src} -mfmt=c -o ${_out}
            DEPENDS ${_src}
            COMMENT "x2native: compiling d3d8_fixed.${st} to SPIR-V")
        list(APPEND X2_SHADER_HEADERS ${_out})
    endforeach()
    add_custom_target(x2_shaders DEPENDS ${X2_SHADER_HEADERS})

    message(STATUS "x2native: linking recompiled modules: ${X2_MODULE_HAVE}")
    add_executable(x2native
        src/native/x2native.c
        src/native/x2native_options.c
        src/config/settings.c
        src/config/settings_store.c
        src/presentation/window_settings.c
        src/input/player_input.c
        src/native/pe_map.c
        src/native/win32_sdl.c
        src/native/guest_heap.c
        src/native/crt.c
        src/recomp/x87crt.c
        src/native/kernel32.c
        src/native/advapi32.c
        src/native/ole32.c
        src/native/gdi32.c
        src/native/dinput.c
        src/native/dinput8.c
        src/native/dinput_device.c
        src/native/dinput_system.c
        src/native/dinput_fifo.c
        src/native/dinput_script.c
        src/native/dinput_joystick.c
        src/native/dinput_pad.c
        src/native/pad_glyphs.c
        src/native/prompt_glyph_pack.c
        src/native/prompt_labels.c
        src/native/input_bindings.c
        src/native/input_probe.c
        src/native/xbox_defaults.c
        src/native/dsound.c
        src/native/guest_clock.c
        src/native/control.c
        src/native/control_png.c
        src/native/ws2_32.c
        src/native/advapi32.c
        src/native/shell32.c
        src/native/winmm.c
        src/native/threads.c
        src/native/x86rt_native.c
        src/native/x86_record.c
        src/native/env_file.c
        src/native/startup.c
        src/native/movie.c
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
        src/d3d8/d3d8_surface.c
        src/d3d8/d3d8_resource.c
        src/d3d8/d3d8_drawcall.c
        src/d3d8/d3d8_vertex_shader.c
        src/d3d8/d3d8_screen_space_test.c
        src/d3d8/d3d8_report.c
        src/vulkan/igvk_ark.c
        src/vulkan/igvk_probe.c
        src/vulkan/igvk_context.c
        src/gpu/gpu_device.c
        src/gpu/gpu_draw.c
        src/gpu/gpu_selftest.c
        src/vulkan/igvk_slots_lifecycle.c
        src/vulkan/igvk_slots_frame.c
        
        ${X2_MODULE_SRCS}
    )
    add_dependencies(x2native x2_shaders x2_pad_glyph_header)
    target_include_directories(x2native PRIVATE src/recomp src/native src/vulkan
                                                src/config src/presentation src/ui
                                                src/input
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
    target_link_options(x2native PRIVATE ${RECOMP_PROBE_WRAPS})

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
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_emitted.py"
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
    target_link_options(x2native PRIVATE -pie)
    # libm: the exe's x87 translations use atan2l/fmodl and friends.
    # -pthread: the heartbeat (src/native/heartbeat.c) runs on its own thread
    # so that a guest stuck anywhere still gets a liveness line.
    target_link_libraries(x2native PRIVATE ${X2_SDL} m pthread)
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
