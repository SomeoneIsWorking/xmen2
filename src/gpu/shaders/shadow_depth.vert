#version 450

layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 v_uv;

layout(set = 1, binding = 0) uniform ShadowCasterState {
    mat4 shadow_mvp;
} shadow;

void main()
{
    gl_Position = shadow.shadow_mvp * in_pos;
    v_uv = in_uv;
}
