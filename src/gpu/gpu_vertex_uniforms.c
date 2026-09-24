/* See gpu_vertex_uniforms.h. */
#include "gpu_vertex_uniforms.h"

#ifdef X2_WITH_SDL
#include "gpu_internal.h"

#include <string.h>

/*
 * MUST match the std140 layout of the vertex shader's uniform block, field for
 * field -- see src/gpu/shaders/d3d8_vertex_stage.glsl. std140 aligns a mat4 and
 * a vec4 to 16 bytes, which is why the uint groups come in fours: a mismatch
 * here does not fail to compile, it silently shifts every field after it.
 */
typedef struct {
  float mvp[16];
  float viewport[4];
  uint32_t pretransformed;
  /* 0 when the vertex format carries NO diffuse colour. The attribute is
     still bound (a missing binding is a validation error) but it points at
     the position's bytes, so the shader must not read it -- reading it
     painted every unlit surface with the float bits of its own X
     coordinate, which is a smooth rainbow across a hillside and reads as a
     lighting bug. */
  uint32_t has_diffuse;
  uint32_t lighting;
  uint32_t nlights;

  float world[16];
  float global_ambient[4];
  float mat_diffuse[4];
  float mat_ambient[4];
  float mat_emissive[4];
  uint32_t has_normal;
  uint32_t color_vertex;
  uint32_t has_specular;
  uint32_t normalize_normals;
  uint32_t diffuse_source;
  uint32_t ambient_source;
  uint32_t emissive_source;
  /* std140: these complete a 16-byte row, so `worldview` below starts
     aligned and the light array that follows it keeps its offset. */
  uint32_t texgen;
  uint32_t programmable;
  uint32_t material_source_pad[3];
  float worldview[16];
  uint32_t texture_transform;
  uint32_t texture_transform_pad[3];
  float texture_matrix[16];
  uint32_t texgen1;
  uint32_t texture_transform1;
  uint32_t stage1_pad[2];
  float texture_matrix1[16];
  /* Five vec4s per light: diffuse, ambient, (position, range),
     (direction, type), (attenuation, unused). */
  float light[GPU_MAX_LIGHTS * 5][4];
  float shadow_mvp[16];
  uint32_t shadow_enabled;
  uint32_t shadow_pad[3];
} VertexUniforms;

/* The block each draw pushes, kept between draws rather than cleared for
   each: clearing its 1.1 KB was 9% of gpu_draw. Every field is written on
   every draw except the lighting block, read only when `lighting` is set, and
   shadow_mvp, read only when `shadow_enabled` is -- and those gates are
   written every draw (the fragment stage's own shadow gate, in the pixel
   block, is rebuilt from zero every draw). The pads stay zero from here. */
static VertexUniforms g_vu;

void gpu_vertex_uniforms_push(SDL_GPUCommandBuffer *command, const GpuDraw *d,
                              const GpuShadowSample *shadow) {
  memcpy(g_vu.mvp, d->mvp, sizeof g_vu.mvp);
  g_vu.viewport[0] = 0.0f;
  g_vu.viewport[1] = 0.0f;
  g_vu.viewport[2] = (float)g_swap_w;
  g_vu.viewport[3] = (float)g_swap_h;
  g_vu.pretransformed = d->pretransformed ? 1u : 0u;
  g_vu.programmable = d->programmable ? 1u : 0u;
  g_vu.has_diffuse = d->color_offset >= 0 ? 1u : 0u;
  g_vu.has_normal = d->normal_offset >= 0 ? 1u : 0u;
  g_vu.has_specular = d->specular_offset >= 0 ? 1u : 0u;
  g_vu.lighting = d->lighting ? 1u : 0u;
  g_vu.color_vertex = d->color_vertex ? 1u : 0u;
  g_vu.normalize_normals = d->normalize_normals ? 1u : 0u;
  g_vu.diffuse_source = d->diffuse_source;
  g_vu.ambient_source = d->ambient_source;
  g_vu.emissive_source = d->emissive_source;
  g_vu.texgen = (uint32_t)d->texgen;
  memcpy(g_vu.worldview, d->worldview, sizeof g_vu.worldview);
  g_vu.texture_transform = d->texture_transform;
  memcpy(g_vu.texture_matrix, d->texture_matrix, sizeof g_vu.texture_matrix);
  g_vu.texgen1 = (uint32_t)d->texgen1;
  g_vu.texture_transform1 = (uint32_t)d->texture_transform1;
  memcpy(g_vu.texture_matrix1, d->texture_matrix1, sizeof g_vu.texture_matrix1);
  if (d->lighting) {
    int li;
    memcpy(g_vu.world, d->world, sizeof g_vu.world);
    memcpy(g_vu.global_ambient, d->global_ambient, sizeof g_vu.global_ambient);
    memcpy(g_vu.mat_diffuse, d->mat_diffuse, sizeof g_vu.mat_diffuse);
    memcpy(g_vu.mat_ambient, d->mat_ambient, sizeof g_vu.mat_ambient);
    memcpy(g_vu.mat_emissive, d->mat_emissive, sizeof g_vu.mat_emissive);
    g_vu.nlights =
        (uint32_t)(d->nlights > GPU_MAX_LIGHTS ? GPU_MAX_LIGHTS : d->nlights);
    for (li = 0; li < (int)g_vu.nlights; li++) {
      const GpuLight *L = &d->light[li];
      memcpy(g_vu.light[li * 5 + 0], L->diffuse, sizeof L->diffuse);
      memcpy(g_vu.light[li * 5 + 1], L->ambient, sizeof L->ambient);
      memcpy(g_vu.light[li * 5 + 2], L->position, 3 * sizeof(float));
      g_vu.light[li * 5 + 2][3] = L->range;
      memcpy(g_vu.light[li * 5 + 3], L->direction, 3 * sizeof(float));
      g_vu.light[li * 5 + 3][3] = (float)L->type;
      memcpy(g_vu.light[li * 5 + 4], L->atten, sizeof L->atten);
      g_vu.light[li * 5 + 4][3] = 0.0f;
    }
  } else {
    g_vu.nlights = 0;
  }
  if (shadow->enabled) {
    memcpy(g_vu.shadow_mvp, shadow->matrix, sizeof g_vu.shadow_mvp);
    g_vu.shadow_enabled = 1;
  } else {
    g_vu.shadow_enabled = 0;
  }
  SDL_PushGPUVertexUniformData(command, 0, &g_vu, sizeof g_vu);
  if (d->vs_program)
    gpu_vs_program_push(command, d->vs_program, d->vs_constants);
}
#endif /* X2_WITH_SDL */
