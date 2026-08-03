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
)
target_include_directories(x2_core PUBLIC src/core)

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
