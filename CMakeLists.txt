cmake_minimum_required(VERSION 3.16)
project(xmen2_sdl LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

add_library(x2_core
    src/core/igb.c
    src/core/igb_mesh.c
    src/core/igb_raster.c
    src/core/igb_anim.c
)
target_include_directories(x2_core PUBLIC src/core)
target_link_libraries(x2_core PUBLIC m)

add_library(x2_display
    src/display/ig_controller.c
    src/display/ig_sdl_controller.c
)
target_include_directories(x2_display PUBLIC src/display)
target_link_libraries(x2_display PUBLIC ${X2_SDL})

enable_testing()
add_executable(test_controller tests/test_controller.c)
target_link_libraries(test_controller PRIVATE x2_display)
add_test(NAME controller COMMAND test_controller)

add_executable(test_enbaya tests/test_enbaya.c)
target_link_libraries(test_enbaya PRIVATE x2_core)
add_test(NAME enbaya COMMAND test_enbaya)

# Known-answer tests for the ADC/SBB flag model (tests/test_flags.c). Header-
# only, so it needs no recompiled output and runs in the ordinary test suite --
# the defect it covers was invisible for as long as nothing tested the runtime
# in isolation.
add_executable(test_flags tests/test_flags.c)
target_include_directories(test_flags PRIVATE src/recomp)
add_test(NAME flags COMMAND test_flags)

# The guest printf format walker. It builds crt.c against a stub runtime, so it
# needs no recompiled output and runs in the ordinary suite.
add_executable(test_vformat tests/test_vformat.c src/native/crt.c
               src/native/guest_heap.c)
target_include_directories(test_vformat PRIVATE src/recomp src/native)
target_compile_definitions(test_vformat PRIVATE _GNU_SOURCE)
target_link_libraries(test_vformat PRIVATE m)
add_test(NAME vformat COMMAND test_vformat)

# The translator, tested as a unit.
#
# tools/recomp.py had no tests, and it is the most defect-prone thing here: a
# mistranslation produces a binary that runs and is wrong. The only oracle was
# a full re-lift and a game run -- ~40 minutes, reporting "stopped at
# <address>" rather than which rule is wrong. These run in milliseconds and
# name the case. See tests/test_recomp.py.
add_test(NAME recomp
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tests/test_recomp.py)

# The host D3D8 interface tables, against a real d3d8.h and against the game's
# own call sites. Runs the checker's SELF-TEST, not the check: the check needs
# scratch/recomp/libIGGfx.json, which is generated and gitignored, while the
# self-test proves the checks can fail and is meaningful with or without it.
add_test(NAME d3d8_abi
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${CMAKE_SOURCE_DIR}/tools/d3d8_abi_check.py --selftest)

add_executable(igb_dump tools/igb_dump.c)
target_link_libraries(igb_dump PRIVATE x2_core)

# The asset viewers stay on SDL2 deliberately. They are separate binaries that
# do not link x2_display, so there is no two-SDLs-in-one-process hazard, and
# converting ~90 call sites of SDL_Renderer code buys the port nothing. The
# PORT path -- x2_display and x2native -- is SDL3.
add_executable(x2view src/app/x2view.c)
target_link_libraries(x2view PRIVATE x2_core)
if(TARGET SDL2::SDL2)
    target_link_libraries(x2view PRIVATE SDL2::SDL2)
else()
    target_link_libraries(x2view PRIVATE PkgConfig::SDL2)
endif()

add_executable(meshview src/app/meshview.c)
target_link_libraries(meshview PRIVATE x2_core m)
if(TARGET SDL2::SDL2)
    target_link_libraries(meshview PRIVATE SDL2::SDL2)
else()
    target_link_libraries(meshview PRIVATE PkgConfig::SDL2)
endif()

add_executable(flyview src/app/flyview.c)
target_link_libraries(flyview PRIVATE x2_core m)
if(TARGET SDL2::SDL2)
    target_link_libraries(flyview PRIVATE SDL2::SDL2)
else()
    target_link_libraries(flyview PRIVATE PkgConfig::SDL2)
endif()

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
set(X2_MODULES libIGDisplay libIGCore libIGSg libIGMath libIGAttrs
               libIGGfx libIGUtils libIGGui libIGLua libIGOpt
               libMovie libCriMovie XMen2)
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
        src/native/pe_map.c
        src/native/win32_sdl.c
        src/native/guest_heap.c
        src/native/crt.c
        src/native/kernel32.c
        src/native/advapi32.c
        src/native/ole32.c
        src/native/gdi32.c
        src/native/dinput.c
        src/native/x86rt_native.c
        src/native/overrides.c
        src/d3d8/d3d8_com.c
        src/d3d8/d3d8_caps.c
        src/d3d8/d3d8_state.c
        src/d3d8/d3d8_d3d8.c
        src/d3d8/d3d8_device.c
        src/d3d8/d3d8_surface.c
        src/d3d8/d3d8_resource.c
        src/d3d8/d3d8_drawcall.c
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
    add_dependencies(x2native x2_shaders)
    target_include_directories(x2native PRIVATE src/recomp src/native src/vulkan
                                              src/gpu src/d3d8
                                              ${CMAKE_BINARY_DIR})

    # Native overrides of DIRECTLY-called recompiled functions.
    #
    # The emitted code calls guest functions as ordinary C functions, so a
    # runtime dispatch table never sees them -- only INDIRECT calls reach the
    # dispatcher. --wrap redirects every call site to __wrap_<name> while
    # keeping the recompiled body linked as __real_<name>, so an override can
    # defer to the original and the two stay diffable.
    #
    # BOTH halves are generated from src/native/overrides.json by
    # tools/gen_overrides.py: these flags, and the --isolate list that puts each
    # overridden function in its own translation unit. --wrap only intercepts
    # calls that cross an object file, so an override sharing a chunk with its
    # caller is bound at compile time and silently never fires.
    set(RECOMP_OVERRIDES_CMAKE
        "${CMAKE_SOURCE_DIR}/src/recomp/gen/recomp_overrides.cmake")
    if(NOT EXISTS "${RECOMP_OVERRIDES_CMAKE}")
        message(FATAL_ERROR
            "Missing ${RECOMP_OVERRIDES_CMAKE}.\n"
            "It is generated by tools/gen_overrides.py from "
            "src/native/overrides.json. Building without it would link a "
            "binary whose native overrides are all silently absent, which "
            "looks exactly like a working build. Run tools/gen_overrides.py.")
    endif()
    include("${RECOMP_OVERRIDES_CMAKE}")
    target_link_options(x2native PRIVATE ${RECOMP_OVERRIDE_WRAPS})
    target_compile_definitions(x2native PRIVATE X2_WITH_SDL _GNU_SOURCE)
    # -DX2_NATIVE_TRACE=ON traces every recompiled body into the boundary ring.
    # Off by default: it costs a ring write per call, and the generated bodies
    # are otherwise IDENTICAL between the two builds, so what a traced run shows
    # applies to an untraced one.
    if(X2_NATIVE_TRACE)
        target_compile_definitions(x2native PRIVATE X86_NATIVE_TRACE)
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
    target_link_libraries(x2native PRIVATE ${X2_SDL} m)
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
             COMMAND x2native --vk-selftest)
    set_tests_properties(vk_frame_path PROPERTIES SKIP_RETURN_CODE 77)
    # The host D3D8's ABI tables, vtable dispatch and caps block. Also needs
    # no game: what it checks is that slot N reaches method N and that an
    # unimplemented method REPORTS, which are host-side facts.
    add_test(NAME d3d8_host
             COMMAND x2native --d3d8-selftest)
    set_tests_properties(d3d8_host PROPERTIES SKIP_RETURN_CODE 77)
else()
    message(STATUS
        "x2native: SKIPPED -- no module has generated output. "
        "Run tools/recomp.py emit and tools/recomp.py native (see above).")
endif()
