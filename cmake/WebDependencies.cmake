# Browser dependencies are consumed from shared/web-port, never desktop packages.
if(NOT X2_WEB_PORT_PREFIX)
    message(FATAL_ERROR "x2native: web build requires shared/web-port; use tools/build_web.py")
endif()
set(_x2_web_manifest "${X2_WEB_PORT_PREFIX}/web-port-dependencies.json")
if(NOT EXISTS "${_x2_web_manifest}")
    message(FATAL_ERROR "x2native: missing browser dependency manifest: ${_x2_web_manifest}")
endif()
file(READ "${_x2_web_manifest}" _x2_web_contract)
string(JSON _x2_web_threads GET "${_x2_web_contract}" pthread)
if(NOT _x2_web_threads)
    message(FATAL_ERROR "x2native: browser dependencies must support pthreads")
endif()
find_package(SDL3 CONFIG REQUIRED PATHS "${X2_WEB_PORT_PREFIX}/lib/cmake/SDL3"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(SDL3_image CONFIG REQUIRED
             PATHS "${X2_WEB_PORT_PREFIX}/lib/cmake/SDL3_image"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(Freetype CONFIG REQUIRED
             PATHS "${X2_WEB_PORT_PREFIX}/lib/cmake/freetype"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
set(X2_SDL SDL3::SDL3-static)
set(X2_SDL_IMAGE SDL3_image::SDL3_image-static)
add_library(x2_web_ffmpeg INTERFACE)
target_include_directories(x2_web_ffmpeg INTERFACE "${X2_WEB_PORT_PREFIX}/include")
foreach(_x2_library avformat avcodec swscale swresample avutil)
    set(_x2_library_path "${X2_WEB_PORT_PREFIX}/lib/lib${_x2_library}.a")
    if(NOT EXISTS "${_x2_library_path}")
        message(FATAL_ERROR "x2native: incomplete browser dependency prefix: ${_x2_library_path}")
    endif()
    target_link_libraries(x2_web_ffmpeg INTERFACE "${_x2_library_path}")
endforeach()
set(X2_FFMPEG_TARGET x2_web_ffmpeg)
