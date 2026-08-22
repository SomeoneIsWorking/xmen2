#include "shadow_policy.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { assert(condition); checks++; } while (0)

static void identity(float matrix[16])
{
    memset(matrix, 0, sizeof(float) * 16);
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static GpuDraw scene_draw(void)
{
    GpuDraw draw;
    memset(&draw, 0, sizeof draw);
    draw.vertex_stride = 24;
    draw.pos_offset = 0;
    draw.normal_offset = 12;
    draw.prim = GPU_PRIM_TRIANGLELIST;
    draw.prim_count = 2;
    draw.depth_test = 1;
    draw.depth_write = 1;
    draw.lighting = 1;
    draw.nlights = 1;
    draw.light[0].type = GPU_LIGHT_DIRECTIONAL;
    draw.light[0].diffuse[0] = draw.light[0].diffuse[1] = 1.0f;
    draw.light[0].diffuse[2] = draw.light[0].diffuse[3] = 1.0f;
    draw.light[0].direction[1] = -1.0f;
    draw.light[0].direction[2] = -1.0f;
    identity(draw.world);
    identity(draw.mvp);
    return draw;
}

int main(void)
{
    GpuDraw draw = scene_draw();
    GpuShadowFramePolicy frame;
    float matrix[16];
    unsigned roles = gpu_shadow_draw_roles(&draw);

    CHECK((roles & GPU_SHADOW_CASTER) != 0);
    CHECK((roles & GPU_SHADOW_RECEIVER) != 0);
    CHECK(gpu_shadow_frame_policy(&draw, &frame));
    gpu_shadow_draw_matrix(&frame, &draw, matrix);
    for (unsigned i = 0; i < 16; i++) CHECK(isfinite(matrix[i]));

    draw.programmable = 1;
    roles = gpu_shadow_draw_roles(&draw);
    CHECK((roles & GPU_SHADOW_CASTER) != 0);
    CHECK((roles & GPU_SHADOW_RECEIVER) != 0);
    gpu_shadow_draw_matrix(&frame, &draw, matrix);
    for (unsigned i = 0; i < 16; i++) CHECK(isfinite(matrix[i]));

    draw.alpha_test = 1;
    draw.uv_offset = 16;
    CHECK((gpu_shadow_draw_roles(&draw) & GPU_SHADOW_CASTER) != 0);
    draw.uv_offset = -1;
    CHECK(gpu_shadow_draw_roles(&draw) == GPU_SHADOW_NONE);
    draw.alpha_test = 0;
    draw.blend_enable = 1;
    CHECK(gpu_shadow_draw_roles(&draw) == GPU_SHADOW_NONE);
    draw.blend_enable = 0;
    draw.pretransformed = 1;
    CHECK(gpu_shadow_draw_roles(&draw) == GPU_SHADOW_NONE);

    printf("test_shadow_policy: %d checks passed\n", checks);
    return 0;
}
