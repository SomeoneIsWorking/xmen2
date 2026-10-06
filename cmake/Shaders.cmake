# Embed the fixed-function shader sources in the format required by the host.
# Browser builds use web-port's WGSL converter and do not need the native
# glslc compiler; native builds keep the explicit compiler requirement.
if(EMSCRIPTEN)
    set(_x2_shader_converter "${X2_WEB_PORT_SOURCE}/tools/shaders.py")
    if(NOT EXISTS "${_x2_shader_converter}")
        message(FATAL_ERROR "Browser shader conversion requires shared/web-port; use tools/build_web.py")
    endif()
endif()
if(NOT EMSCRIPTEN)
    find_program(X2_GLSLC NAMES glslc REQUIRED)
endif()
set(X2_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${X2_SHADER_DIR})
set(X2_SHADER_HEADERS "")
# Every entry point, and the files they include: an edit to an include must
# rebuild each entry that pulls it in.
set(X2_SHADER_ENTRIES d3d8_fixed.vert d3d8_fixed.frag d3d8_vs11.vert
    shadow_depth.vert shadow_depth.frag shadow_vs11.vert)
set(X2_SHADER_INCLUDES
    ${CMAKE_SOURCE_DIR}/src/gpu/shaders/d3d8_vertex_stage.glsl
    ${CMAKE_SOURCE_DIR}/src/gpu/shaders/vs11_program.glsl)
foreach(entry IN LISTS X2_SHADER_ENTRIES)
    string(REGEX REPLACE "\\.(vert|frag)$" "" shader "${entry}")
    string(REGEX REPLACE "^.*\\." "" st "${entry}")
    set(_src ${CMAKE_SOURCE_DIR}/src/gpu/shaders/${entry})
    set(_out ${X2_SHADER_DIR}/${shader}_${st}.inc)
    if(EMSCRIPTEN)
        set(_depth_args "")
        if(shader STREQUAL "d3d8_fixed" AND st STREQUAL "frag")
            # Fragment set 2, SDL sampler slot 3 is the raw depth view.
            set(_depth_args --depth-sampler 2:3)
        endif()
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${Python3_EXECUTABLE} ${_x2_shader_converter}
                    convert ${_src} ${_out} --stage ${st} --include ${_depth_args}
            DEPENDS ${_src} ${X2_SHADER_INCLUDES} ${_x2_shader_converter}
                    "${X2_WEB_PORT_SOURCE}/tools/shader_depth.py"
                    "${X2_WEB_PORT_SOURCE}/tools/shader_arrays.py"
            COMMENT "x2native: compiling ${shader}.${st} to WGSL"
            VERBATIM)
    else()
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${X2_GLSLC} -fshader-stage=${st} ${_src} -mfmt=c -o ${_out}
            DEPENDS ${_src} ${X2_SHADER_INCLUDES}
            COMMENT "x2native: compiling ${shader}.${st} to SPIR-V"
            VERBATIM)
    endif()
    list(APPEND X2_SHADER_HEADERS ${_out})
endforeach()
add_custom_target(x2_shaders DEPENDS ${X2_SHADER_HEADERS})
