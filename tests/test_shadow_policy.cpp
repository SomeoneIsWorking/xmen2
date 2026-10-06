#include "shadow_policy.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "test_shadow_policy: line %d failed: %s\n", __LINE__,    \
              #condition);                                                     \
      exit(1);                                                                 \
    }                                                                          \
    checks++;                                                                  \
  } while (0)

static const uint32_t RESOLUTION = 1024;
static const float NEAR_PLANE = 50.0f, FAR_PLANE = 4000.0f;
static const float TAN_HALF_FOV = 0.57735f, ASPECT = 1.3333f;
static const float EYE_X = 100.0f, EYE_Y = 300.0f, EYE_Z = -200.0f;

static void identity(float matrix[16]) {
  memset(matrix, 0, sizeof(float) * 16);
  matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static GpuDraw scene_draw(void) {
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

/* A camera at (x, y, z) looking down +z with a D3D left-handed projection, in
 * the row-vector convention: clip = [p 1] * mvp. */
static GpuDraw camera_draw(float x, float y, float z) {
  GpuDraw draw = scene_draw();
  float range = FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
  memset(draw.mvp, 0, sizeof draw.mvp);
  draw.mvp[0] = 1.0f / (TAN_HALF_FOV * ASPECT);
  draw.mvp[5] = 1.0f / TAN_HALF_FOV;
  draw.mvp[10] = range;
  draw.mvp[11] = 1.0f;
  draw.mvp[12] = -x * draw.mvp[0];
  draw.mvp[13] = -y * draw.mvp[5];
  draw.mvp[14] = -(z + NEAR_PLANE) * range;
  draw.mvp[15] = -z;
  return draw;
}

static void light_clip(const float matrix[16], const float p[3], float out[3]) {
  for (unsigned column = 0; column < 3; column++)
    out[column] = p[0] * matrix[column] + p[1] * matrix[4 + column] +
                  p[2] * matrix[8 + column] + matrix[12 + column];
}

static void test_roles_and_matrices(void) {
  GpuDraw draw = scene_draw();
  GpuShadowFramePolicy frame;
  float matrix[16];
  unsigned roles = gpu_shadow_draw_roles(&draw);

  CHECK((roles & GPU_SHADOW_CASTER) != 0);
  CHECK((roles & GPU_SHADOW_RECEIVER) != 0);
  /* The identity camera is orthographic: view depth runs along its axis. */
  CHECK(gpu_shadow_frame_policy(&draw, RESOLUTION, &frame));
  CHECK(frame.view_near == 0.0f && fabsf(frame.view_far - 1.0f) < 1e-5f);
  for (unsigned c = 0; c < GPU_SHADOW_CASCADES; c++) {
    gpu_shadow_draw_matrix(&frame, c, &draw, matrix);
    for (unsigned i = 0; i < 16; i++)
      CHECK(isfinite(matrix[i]));
  }

  draw.programmable = 1;
  roles = gpu_shadow_draw_roles(&draw);
  CHECK((roles & GPU_SHADOW_CASTER) != 0);
  CHECK((roles & GPU_SHADOW_RECEIVER) != 0);
  gpu_shadow_draw_matrix(&frame, 0, &draw, matrix);
  for (unsigned i = 0; i < 16; i++)
    CHECK(isfinite(matrix[i]));

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
}

static void test_splits_and_fit(void) {
  GpuDraw draw = camera_draw(EYE_X, EYE_Y, EYE_Z);
  GpuShadowFramePolicy frame;
  CHECK(gpu_shadow_frame_policy(&draw, RESOLUTION, &frame));
  CHECK(fabsf(frame.view_near - NEAR_PLANE) < 0.1f);
  CHECK(fabsf(frame.view_far - FAR_PLANE) < 1.0f);
  CHECK(frame.cascade[0].slice_near == frame.view_near);
  CHECK(frame.cascade[GPU_SHADOW_CASCADES - 1].split_far == frame.view_far);
  for (unsigned c = 0; c < GPU_SHADOW_CASCADES; c++) {
    const GpuShadowCascade *cascade = &frame.cascade[c];
    CHECK(cascade->split_far > cascade->slice_near);
    CHECK(cascade->blend_start > cascade->slice_near &&
          cascade->blend_start < cascade->split_far);
    if (c) {
      const GpuShadowCascade *previous = &frame.cascade[c - 1];
      CHECK(cascade->split_far > previous->split_far);
      /* Each slice starts where the previous cascade's blend band does. */
      CHECK(cascade->slice_near == previous->blend_start);
      CHECK(cascade->texel_world > previous->texel_world);
    }
    for (unsigned corner = 0; corner < 8; corner++) {
      float depth = corner < 4 ? cascade->slice_near : cascade->split_far;
      float half_y = depth * TAN_HALF_FOV, half_x = half_y * ASPECT;
      float world[3] = {EYE_X + (corner & 1 ? half_x : -half_x),
                        EYE_Y + (corner & 2 ? half_y : -half_y), EYE_Z + depth};
      float clip[3];
      light_clip(cascade->light_view_projection, world, clip);
      CHECK(clip[0] >= -1.0001f && clip[0] <= 1.0001f);
      CHECK(clip[1] >= -1.0001f && clip[1] <= 1.0001f);
      CHECK(clip[2] >= -0.0001f && clip[2] <= 1.0001f);
    }
    /* A caster a thousand units toward the light, outside the view, still
     * lands inside the depth range. */
    {
      float on_slice[3] = {EYE_X, EYE_Y, EYE_Z + cascade->slice_near};
      float lifted[3], clip[3];
      for (unsigned axis = 0; axis < 3; axis++)
        lifted[axis] = on_slice[axis] - frame.light_direction[axis] * 1000.0f;
      light_clip(cascade->light_view_projection, lifted, clip);
      CHECK(clip[2] >= 0.0f && clip[2] <= 1.0f);
    }
  }
  /* The old single fit left 6.85 world units per texel. */
  CHECK(frame.cascade[0].texel_world < 1.0f);
}

static void test_texel_snapping(void) {
  GpuDraw base = camera_draw(500.0f, EYE_Y, 0.0f);
  GpuShadowFramePolicy reference;
  CHECK(gpu_shadow_frame_policy(&base, RESOLUTION, &reference));
  for (unsigned c = 0; c < GPU_SHADOW_CASCADES; c++) {
    const float texel = reference.cascade[c].texel_world;
    float previous[16];
    unsigned changes = 0;
    for (unsigned step = 0; step < 20; step++) {
      /* This light's right axis is world x, so the camera slides along it. */
      GpuDraw draw =
          camera_draw(500.0f + (float)step * 0.05f * texel, EYE_Y, 0.0f);
      GpuShadowFramePolicy frame;
      const float *matrix;
      CHECK(gpu_shadow_frame_policy(&draw, RESOLUTION, &frame));
      matrix = frame.cascade[c].light_view_projection;
      if (step) {
        for (unsigned i = 0; i < 16; i++)
          if (i != 12)
            CHECK(matrix[i] == previous[i]);
        if (matrix[12] != previous[12])
          changes++;
      }
      memcpy(previous, matrix, sizeof previous);
    }
    /* Moving under one texel shifts the snapped centre at most once. */
    CHECK(changes <= 1);
    {
      GpuDraw moved = camera_draw(500.0f + 3.0f * texel, EYE_Y, 0.0f);
      GpuShadowFramePolicy shifted;
      CHECK(gpu_shadow_frame_policy(&moved, RESOLUTION, &shifted));
      CHECK(shifted.cascade[c].light_view_projection[12] !=
            reference.cascade[c].light_view_projection[12]);
    }
  }
}

int main(void) {
  test_roles_and_matrices();
  test_splits_and_fit();
  test_texel_snapping();
  printf("test_shadow_policy: %d checks passed\n", checks);
  return 0;
}
