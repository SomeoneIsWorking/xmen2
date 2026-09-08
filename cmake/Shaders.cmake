# Embed the fixed-function shader sources in the format required by the host.
find_program(X2_GLSLC NAMES glslc REQUIRED)
if(EMSCRIPTEN)
    set(_x2_shader_converter "${X2_WEB_PORT_SOURCE}/tools/shaders.py")
    if(NOT EXISTS "${_x2_shader_converter}")
        message(FATAL_ERROR "Browser shader conversion requires shared/web-port; use tools/build_web.py")
    endif()
endif()
set(X2_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${X2_SHADER_DIR})
set(X2_SHADER_HEADERS "")
foreach(shader d3d8_fixed shadow_depth)
    foreach(st vert frag)
        set(_src ${CMAKE_SOURCE_DIR}/src/gpu/shaders/${shader}.${st})
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
                DEPENDS ${_src} ${_x2_shader_converter}
                        "${X2_WEB_PORT_SOURCE}/tools/shader_depth.py"
                        "${X2_WEB_PORT_SOURCE}/tools/shader_arrays.py"
                COMMENT "x2native: compiling ${shader}.${st} to WGSL"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT ${_out}
                COMMAND ${X2_GLSLC} -fshader-stage=${st} ${_src} -mfmt=c -o ${_out}
                DEPENDS ${_src}
                COMMENT "x2native: compiling ${shader}.${st} to SPIR-V"
                VERBATIM)
        endif()
        list(APPEND X2_SHADER_HEADERS ${_out})
    endforeach()
endforeach()
add_custom_target(x2_shaders DEPENDS ${X2_SHADER_HEADERS})
