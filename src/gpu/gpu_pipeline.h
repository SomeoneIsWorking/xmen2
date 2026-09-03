#ifndef GPU_PIPELINE_H
#define GPU_PIPELINE_H

/*
 * The pipeline and sampler caches.
 *
 * Split from gpu_draw.c, which decides WHAT to draw. This owns the objects a
 * draw needs before it can be recorded: the fixed-function shaders, one baked
 * pipeline per distinct render state, and one sampler per distinct filtering
 * state. Both caches are bounded and refuse by name when full rather than
 * silently reusing the wrong object.
 */
#ifdef X2_WITH_SDL
#include "gpu_draw.h"

#include <SDL3/SDL.h>
#include <stdint.h>

/*
 * The pipeline cache.
 *
 * A Vulkan pipeline bakes in everything D3D8 changes with a SetRenderState
 * call, so one is built per distinct combination and kept. The key is the
 * struct below compared byte for byte -- which is only sound because it is
 * memset to zero before its fields are filled, so padding cannot differ
 * between two otherwise-identical keys.
 */
typedef struct {
  uint32_t stride;
  int pos_offset, color_offset, specular_offset, uv_offset, normal_offset;
  int pos_is_float4, color_is_float4, specular_is_float4;
  int prim;
  int blend_enable, src_blend, dst_blend;
  int depth_test, depth_write, depth_func;
  int cull;
  int pretransformed;
  int has_depth_target;
} PipeKey;

/* The cached pipeline for this state, built on first use. NULL means it could
   not be built, and that has been reported. */
SDL_GPUGraphicsPipeline *gpu_pipeline_for(const PipeKey *k);

/* The cached sampler for this filtering state; NULL is a reported failure. */
SDL_GPUSampler *gpu_sampler_for(int clamp, int mag_point, int min_filter,
                                int mip, float lod_bias, int max_anisotropy);

/*
 * How many pipelines were EVER built, and how many are cached right now.
 *
 * They are different numbers and the report needs both: teardown empties the
 * cache, and the engine releases the device before the shutdown report runs,
 * so the live count alone read "0 pipeline(s) built" beside half a million
 * draws.
 */
unsigned long gpu_pipelines_built(void);
int gpu_pipelines_cached(void);

/* Release every cached pipeline, sampler and shader. */
void gpu_pipeline_shutdown(void);

#endif /* X2_WITH_SDL */
/*
 * Does this backend have that blend factor at all?
 *
 * The draw path has to refuse a factor the pipeline could not bake, and
 * the factor table lives here -- so it asks rather than keeping a second
 * copy of the mapping that could disagree with the one actually used.
 */
int gpu_blend_supported(GpuBlend b);

#endif /* GPU_PIPELINE_H */
