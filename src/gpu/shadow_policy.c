#include "shadow_policy.h"
#include "gpu_matrix.h"

#include <math.h>
#include <string.h>

static int matrix_inverse(const float in[16], float out[16]) {
  float rows[4][8];
  int column, row, pivot, k;
  for (row = 0; row < 4; row++)
    for (column = 0; column < 8; column++)
      rows[row][column] =
          column < 4 ? in[row * 4 + column] : (column - 4 == row ? 1.0f : 0.0f);
  for (column = 0; column < 4; column++) {
    pivot = column;
    for (row = column + 1; row < 4; row++)
      if (fabsf(rows[row][column]) > fabsf(rows[pivot][column]))
        pivot = row;
    if (!isfinite(rows[pivot][column]) || fabsf(rows[pivot][column]) < 1e-8f)
      return 0;
    if (pivot != column)
      for (k = 0; k < 8; k++) {
        float value = rows[column][k];
        rows[column][k] = rows[pivot][k];
        rows[pivot][k] = value;
      }
    {
      float scale = rows[column][column];
      for (k = 0; k < 8; k++)
        rows[column][k] /= scale;
    }
    for (row = 0; row < 4; row++) {
      float scale;
      if (row == column)
        continue;
      scale = rows[row][column];
      for (k = 0; k < 8; k++)
        rows[row][k] -= scale * rows[column][k];
    }
  }
  for (row = 0; row < 4; row++)
    for (column = 0; column < 4; column++)
      out[row * 4 + column] = rows[row][column + 4];
  return 1;
}

static int vector_normalize(float v[3]) {
  float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!isfinite(length) || length < 1e-6f)
    return 0;
  v[0] /= length;
  v[1] /= length;
  v[2] /= length;
  return 1;
}

static void vector_cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float vector_dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int row_transform(const float in[4], const float matrix[16],
                         float out[4]) {
  int column, k;
  for (column = 0; column < 4; column++) {
    out[column] = 0.0f;
    for (k = 0; k < 4; k++)
      out[column] += in[k] * matrix[k * 4 + column];
  }
  if (!isfinite(out[3]) || fabsf(out[3]) < 1e-7f)
    return 0;
  return 1;
}

unsigned gpu_shadow_draw_roles(const GpuDraw *draw) {
  unsigned roles = GPU_SHADOW_NONE;
  int triangles;
  float emissive;
  if (!draw || draw->pretransformed || draw->pos_offset < 0 ||
      !draw->depth_test || !draw->depth_write || draw->blend_enable ||
      (draw->alpha_test && draw->uv_offset < 0) || !draw->prim_count)
    return roles;
  triangles = draw->prim == GPU_PRIM_TRIANGLELIST ||
              draw->prim == GPU_PRIM_TRIANGLESTRIP;
  if (!triangles)
    return roles;
  roles |= GPU_SHADOW_CASTER;
  emissive =
      draw->mat_emissive[0] + draw->mat_emissive[1] + draw->mat_emissive[2];
  if (draw->programmable || (draw->lighting && emissive <= 0.0001f))
    roles |= GPU_SHADOW_RECEIVER;
  return roles;
}

int gpu_shadow_frame_policy(const GpuDraw *draw, GpuShadowFramePolicy *out) {
  float inverse_world[16], view_projection[16], inverse_view_projection[16];
  float corners[8][3], right[3], up[3], reference_up[3] = {0.0f, 1.0f, 0.0f};
  float minv[3] = {INFINITY, INFINITY, INFINITY};
  float maxv[3] = {-INFINITY, -INFINITY, -INFINITY};
  const GpuLight *light = NULL;
  int i, x, y, z;

  if (!draw || !out || !draw->lighting || draw->programmable)
    return 0;
  for (i = 0; i < draw->nlights; i++) {
    const GpuLight *candidate = &draw->light[i];
    if (candidate->type == GPU_LIGHT_DIRECTIONAL &&
        (candidate->diffuse[0] != 0.0f || candidate->diffuse[1] != 0.0f ||
         candidate->diffuse[2] != 0.0f)) {
      light = candidate;
      break;
    }
  }
  if (!light || !matrix_inverse(draw->world, inverse_world))
    return 0;
  gpu_matrix_multiply(inverse_world, draw->mvp, view_projection);
  if (!matrix_inverse(view_projection, inverse_view_projection))
    return 0;

  i = 0;
  for (z = 0; z <= 1; z++)
    for (y = -1; y <= 1; y += 2)
      for (x = -1; x <= 1; x += 2) {
        float clip[4] = {(float)x, (float)y, (float)z, 1.0f};
        float world[4];
        if (!row_transform(clip, inverse_view_projection, world))
          return 0;
        corners[i][0] = world[0] / world[3];
        corners[i][1] = world[1] / world[3];
        corners[i][2] = world[2] / world[3];
        i++;
      }

  memcpy(out->light_direction, light->direction, sizeof out->light_direction);
  if (!vector_normalize(out->light_direction))
    return 0;
  if (fabsf(vector_dot(reference_up, out->light_direction)) > 0.98f) {
    reference_up[0] = 1.0f;
    reference_up[1] = reference_up[2] = 0.0f;
  }
  vector_cross(reference_up, out->light_direction, right);
  if (!vector_normalize(right))
    return 0;
  vector_cross(out->light_direction, right, up);
  if (!vector_normalize(up))
    return 0;

  for (i = 0; i < 8; i++) {
    float projected[3] = {vector_dot(corners[i], right),
                          vector_dot(corners[i], up),
                          vector_dot(corners[i], out->light_direction)};
    int axis;
    for (axis = 0; axis < 3; axis++) {
      if (projected[axis] < minv[axis])
        minv[axis] = projected[axis];
      if (projected[axis] > maxv[axis])
        maxv[axis] = projected[axis];
    }
  }
  if (maxv[0] - minv[0] < 1e-4f || maxv[1] - minv[1] < 1e-4f ||
      maxv[2] - minv[2] < 1e-4f)
    return 0;
  memset(out->light_view_projection, 0, sizeof out->light_view_projection);
  {
    float half_x = (maxv[0] - minv[0]) * 0.525f;
    float half_y = (maxv[1] - minv[1]) * 0.525f;
    float center_x = (minv[0] + maxv[0]) * 0.5f;
    float center_y = (minv[1] + maxv[1]) * 0.5f;
    float depth = maxv[2] - minv[2];
    out->light_view_projection[0] = right[0] / half_x;
    out->light_view_projection[4] = right[1] / half_x;
    out->light_view_projection[8] = right[2] / half_x;
    out->light_view_projection[12] = -center_x / half_x;
    out->light_view_projection[1] = up[0] / half_y;
    out->light_view_projection[5] = up[1] / half_y;
    out->light_view_projection[9] = up[2] / half_y;
    out->light_view_projection[13] = -center_y / half_y;
    out->light_view_projection[2] = out->light_direction[0] / depth;
    out->light_view_projection[6] = out->light_direction[1] / depth;
    out->light_view_projection[10] = out->light_direction[2] / depth;
    out->light_view_projection[14] = -minv[2] / depth;
    out->light_view_projection[15] = 1.0f;
  }
  memcpy(out->inverse_view_projection, inverse_view_projection,
         sizeof out->inverse_view_projection);
  return 1;
}

void gpu_shadow_draw_matrix(const GpuShadowFramePolicy *frame,
                            const GpuDraw *draw, float out[16]) {
  gpu_matrix_multiply(draw->programmable ? frame->inverse_view_projection
                                         : draw->world,
                      frame->light_view_projection, out);
}
