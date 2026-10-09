#include "shadow_policy.h"
#include "gpu_matrix.h"

#include <math.h>
#include <string.h>

namespace x2::gpu {

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

static const float SPLIT_LAMBDA = 0.8f;
/* Fraction of a cascade's length blended into the next one. */
static const float BLEND_FRACTION = 0.1f;
static const float DEPTH_BIAS_TEXELS = 1.0f;
static const float NORMAL_OFFSET_TEXELS = 1.5f;
/* Penumbra width of the finest cascade, in its own texels. */
static const float PENUMBRA_TEXELS = 1.5f;
static const float KERNEL_MAX_TEXELS = 1.5f;
/* Sphere radii step by an eighth of an octave so small camera moves keep the
 * same map size. */
static const float RADIUS_STEPS_PER_OCTAVE = 8.0f;

static float view_depth(const float plane[4], const float p[3]) {
  return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
}

static int fit_depth_plane(const float view_projection[16], float corners[8][3],
                           GpuShadowFramePolicy *out) {
  float plane[4] = {view_projection[3], view_projection[7], view_projection[11],
                    view_projection[15]};
  float near_w = view_depth(plane, corners[0]);
  float far_w = view_depth(plane, corners[4]);
  if (fabsf(far_w - near_w) > 1e-4f * fmaxf(fabsf(far_w), fabsf(near_w))) {
    if (near_w <= 0.0f || far_w <= near_w)
      return 0;
    memcpy(out->view_depth_plane, plane, sizeof plane);
    out->view_near = near_w;
    out->view_far = far_w;
    return 1;
  }
  /* An orthographic camera has no eye depth: measure along its view axis. */
  {
    float near_centre[3] = {0.0f, 0.0f, 0.0f}, axis[3] = {0.0f, 0.0f, 0.0f};
    int i, k;
    for (i = 0; i < 4; i++)
      for (k = 0; k < 3; k++) {
        near_centre[k] += corners[i][k] * 0.25f;
        axis[k] += (corners[i + 4][k] - corners[i][k]) * 0.25f;
      }
    out->view_far = sqrtf(vector_dot(axis, axis));
    if (!vector_normalize(axis))
      return 0;
    out->view_depth_plane[0] = axis[0];
    out->view_depth_plane[1] = axis[1];
    out->view_depth_plane[2] = axis[2];
    out->view_depth_plane[3] = -vector_dot(axis, near_centre);
    out->view_near = 0.0f;
  }
  return 1;
}

static void practical_splits(float near_depth, float far_depth,
                             float bounds[GPU_SHADOW_CASCADES + 1]) {
  float log_near = fmaxf(near_depth, far_depth * 1e-3f);
  int i;
  bounds[0] = near_depth;
  bounds[GPU_SHADOW_CASCADES] = far_depth;
  for (i = 1; i < GPU_SHADOW_CASCADES; i++) {
    float fraction = (float)i / (float)GPU_SHADOW_CASCADES;
    float logarithmic = log_near * powf(far_depth / log_near, fraction);
    float linear = near_depth + (far_depth - near_depth) * fraction;
    bounds[i] = SPLIT_LAMBDA * logarithmic + (1.0f - SPLIT_LAMBDA) * linear;
  }
}

/* The frustum between two view depths: corners interpolate along the edges,
 * along which view depth is linear. */
static void slice_corners(const float corners[8][3], float slice_near,
                          float slice_far, float view_near, float view_far,
                          float out[8][3]) {
  int edge, k, end;
  for (end = 0; end < 2; end++) {
    float t =
        ((end ? slice_far : slice_near) - view_near) / (view_far - view_near);
    for (edge = 0; edge < 4; edge++)
      for (k = 0; k < 3; k++)
        out[end * 4 + edge][k] =
            corners[edge][k] + t * (corners[edge + 4][k] - corners[edge][k]);
  }
}

static int fit_cascade(const float slice[8][3], const float right[3],
                       const float up[3], const float direction[3], float reach,
                       uint32_t resolution, GpuShadowCascade *out) {
  float centre[3] = {0.0f, 0.0f, 0.0f}, radius = 0.0f, half, texel, range;
  float light_centre[3], near_z;
  int i, k;
  for (i = 0; i < 8; i++)
    for (k = 0; k < 3; k++)
      centre[k] += slice[i][k] * 0.125f;
  for (i = 0; i < 8; i++) {
    float d[3] = {slice[i][0] - centre[0], slice[i][1] - centre[1],
                  slice[i][2] - centre[2]};
    radius = fmaxf(radius, sqrtf(vector_dot(d, d)));
  }
  if (!isfinite(radius) || radius < 1e-6f)
    return 0;
  radius = exp2f(ceilf(log2f(radius) * RADIUS_STEPS_PER_OCTAVE) /
                 RADIUS_STEPS_PER_OCTAVE);
  /* Room for the snap to move the centre by up to a texel. */
  half = radius * (float)resolution / (float)(resolution - 2);
  texel = 2.0f * half / (float)resolution;
  light_centre[0] = roundf(vector_dot(centre, right) / texel) * texel;
  light_centre[1] = roundf(vector_dot(centre, up) / texel) * texel;
  light_centre[2] = roundf(vector_dot(centre, direction) / texel) * texel;
  /* Casters between the light and the slice sit up to `reach` before it. */
  near_z = light_centre[2] - half - reach;
  range = 2.0f * half + reach;
  memset(out->light_view_projection, 0, sizeof out->light_view_projection);
  for (k = 0; k < 3; k++) {
    out->light_view_projection[k * 4 + 0] = right[k] / half;
    out->light_view_projection[k * 4 + 1] = up[k] / half;
    out->light_view_projection[k * 4 + 2] = direction[k] / range;
  }
  out->light_view_projection[12] = -light_centre[0] / half;
  out->light_view_projection[13] = -light_centre[1] / half;
  out->light_view_projection[14] = -near_z / range;
  out->light_view_projection[15] = 1.0f;
  out->extent[0] = out->extent[1] = 2.0f * half;
  out->texel_world = texel;
  out->depth_range = range;
  out->depth_bias = DEPTH_BIAS_TEXELS * texel / range;
  out->normal_offset = NORMAL_OFFSET_TEXELS * texel;
  return 1;
}

int gpu_shadow_frame_policy(const GpuDraw *draw, uint32_t resolution,
                            GpuShadowFramePolicy *out) {
  float inverse_world[16], view_projection[16], inverse_view_projection[16];
  float corners[8][3], right[3], up[3], reference_up[3] = {0.0f, 1.0f, 0.0f};
  float bounds[GPU_SHADOW_CASCADES + 1];
  const GpuLight *light = NULL;
  int i, x, y, z;

  if (!draw || !out || resolution < 8 || !draw->lighting || draw->programmable)
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
  if (!fit_depth_plane(view_projection, corners, out) ||
      out->view_far - out->view_near < 1e-4f)
    return 0;

  practical_splits(out->view_near, out->view_far, bounds);
  for (i = 0; i < GPU_SHADOW_CASCADES; i++) {
    GpuShadowCascade *cascade = &out->cascade[i];
    float slice[8][3];
    cascade->split_far = bounds[i + 1];
    cascade->blend_start =
        bounds[i + 1] - BLEND_FRACTION * (bounds[i + 1] - bounds[i]);
    cascade->slice_near = i ? out->cascade[i - 1].blend_start : bounds[0];
    slice_corners(corners, cascade->slice_near, cascade->split_far,
                  out->view_near, out->view_far, slice);
    if (!fit_cascade(slice, right, up, out->light_direction,
                     out->view_far - out->view_near, resolution, cascade))
      return 0;
  }
  for (i = 0; i < GPU_SHADOW_CASCADES; i++) {
    float spacing = PENUMBRA_TEXELS * out->cascade[0].texel_world /
                    out->cascade[i].texel_world;
    out->cascade[i].kernel_texels =
        fminf(fmaxf(spacing, 1.0f), KERNEL_MAX_TEXELS);
  }
  memcpy(out->inverse_view_projection, inverse_view_projection,
         sizeof out->inverse_view_projection);
  return 1;
}

void gpu_shadow_draw_matrix(const GpuShadowFramePolicy *frame, unsigned cascade,
                            const GpuDraw *draw, float out[16]) {
  gpu_matrix_multiply(draw->programmable ? frame->inverse_view_projection
                                         : draw->world,
                      frame->cascade[cascade].light_view_projection, out);
}

} // namespace x2::gpu
