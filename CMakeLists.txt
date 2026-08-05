cmake_minimum_required(VERSION 3.16)
project(xmen2_sdl LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
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
if(TARGET SDL2::SDL2)
    target_link_libraries(x2_display PUBLIC SDL2::SDL2)
else()
    target_link_libraries(x2_display PUBLIC PkgConfig::SDL2)
endif()

enable_testing()
add_executable(test_controller tests/test_controller.c)
target_link_libraries(test_controller PRIVATE x2_display)
add_test(NAME controller COMMAND test_controller)

add_executable(test_enbaya tests/test_enbaya.c)
target_link_libraries(test_enbaya PRIVATE x2_core)
add_test(NAME enbaya COMMAND test_enbaya)

add_executable(igb_dump tools/igb_dump.c)
target_link_libraries(igb_dump PRIVATE x2_core)

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
set(X2_RECOMP_BODIES ${CMAKE_SOURCE_DIR}/src/recomp/libIGDisplay.c)
set(X2_RECOMP_NATIVE ${CMAKE_SOURCE_DIR}/src/recomp/libIGDisplay_native.c)
if(EXISTS ${X2_RECOMP_BODIES} AND EXISTS ${X2_RECOMP_NATIVE})
    add_executable(x2native
        src/native/x2native.c
        src/native/pe_map.c
        ${X2_RECOMP_BODIES}
        ${X2_RECOMP_NATIVE}
    )
    target_include_directories(x2native PRIVATE src/recomp src/native)
    target_compile_definitions(x2native PRIVATE X2_WITH_SDL _GNU_SOURCE)
    if(TARGET SDL2::SDL2)
        target_link_libraries(x2native PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(x2native PRIVATE PkgConfig::SDL2)
    endif()
    add_test(NAME native_battery
             COMMAND x2native $ENV{GAME_PC_DIR}/libIGDisplay.dll --no-window)
else()
    message(STATUS
        "x2native: SKIPPED -- generated recompiler output is missing. "
        "Run tools/recomp.py emit and tools/recomp.py native (see above).")
endif()
