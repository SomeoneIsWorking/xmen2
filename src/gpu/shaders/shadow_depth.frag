#version 450

layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D caster_texture;
layout(set = 3, binding = 0) uniform ShadowCasterPixel {
    uint alpha_test;
    float alpha_ref;
} caster;

void main()
{
    if (caster.alpha_test != 0u &&
        texture(caster_texture, v_uv).a < caster.alpha_ref) discard;
}
