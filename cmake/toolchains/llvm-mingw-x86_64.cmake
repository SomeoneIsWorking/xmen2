# Windows x86-64 with llvm-mingw (Clang, libc++, UCRT). tools/windows_deps.py
# provisions the toolchain and passes its root as LLVM_MINGW_ROOT and the
# dependency prefix as CMAKE_FIND_ROOT_PATH.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT LLVM_MINGW_ROOT)
    message(FATAL_ERROR
        "llvm-mingw-x86_64.cmake: pass -DLLVM_MINGW_ROOT=<llvm-mingw directory>; "
        "uv run --frozen python tools/windows_deps.py provisions one.")
endif()
# Windows hosts pass backslashes, which generated CMake files read as escapes.
file(TO_CMAKE_PATH "${LLVM_MINGW_ROOT}" LLVM_MINGW_ROOT)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)

set(_x2_mingw_bin "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32")
set(CMAKE_C_COMPILER "${_x2_mingw_bin}-clang${CMAKE_HOST_EXECUTABLE_SUFFIX}")
set(CMAKE_CXX_COMPILER "${_x2_mingw_bin}-clang++${CMAKE_HOST_EXECUTABLE_SUFFIX}")
set(CMAKE_RC_COMPILER "${_x2_mingw_bin}-windres${CMAKE_HOST_EXECUTABLE_SUFFIX}")
set(CMAKE_AR "${LLVM_MINGW_ROOT}/bin/llvm-ar${CMAKE_HOST_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "")
set(CMAKE_RANLIB "${LLVM_MINGW_ROOT}/bin/llvm-ranlib${CMAKE_HOST_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "")

# libc++, libunwind and winpthreads link into each executable, so nothing but
# system DLLs has to sit beside it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

list(APPEND CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
