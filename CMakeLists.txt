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
               libIGGfx libIGUtils XMen2)
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
    message(STATUS "x2native: linking recompiled modules: ${X2_MODULE_HAVE}")
    add_executable(x2native
        src/native/x2native.c
        src/native/pe_map.c
        src/native/win32_sdl.c
        src/native/guest_heap.c
        src/native/crt.c
        src/native/kernel32.c
        src/native/x86rt_native.c
        ${X2_MODULE_SRCS}
    )
    target_include_directories(x2native PRIVATE src/recomp src/native)
    target_compile_definitions(x2native PRIVATE X2_WITH_SDL _GNU_SOURCE)
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
    add_test(NAME native_battery
             COMMAND x2native --no-window --selftest)
    set_tests_properties(native_battery PROPERTIES SKIP_RETURN_CODE 77)
else()
    message(STATUS
        "x2native: SKIPPED -- no module has generated output. "
        "Run tools/recomp.py emit and tools/recomp.py native (see above).")
endif()
