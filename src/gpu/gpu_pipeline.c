/*
 * Pipeline and sampler caches for the fixed-function draw path.
 *
 * A Vulkan pipeline bakes in everything a D3D8 SetRenderState call changes, so
 * one is built per distinct combination and kept. Both caches are bounded and
 * say so when they fill: a cache that wrapped or evicted silently would draw
 * with the wrong state and look like a shader bug.
 */
#include "gpu_pipeline.h"

#ifdef X2_WITH_SDL
#include "gpu_draw.h"
#include "gpu_internal.h"

#include <stdio.h>
#include <string.h>

/* Compiled from the vertex and fragment files in src/gpu/shaders at build time.
   The declaration lives here so the generated file is nothing but SPIR-V. */
static const unsigned int d3d8_fixed_vert_spv[] =
#include "shaders/d3d8_fixed_vert.inc"
;
static const unsigned int d3d8_fixed_frag_spv[] =
#include "shaders/d3d8_fixed_frag.inc"
;

static SDL_GPUShader *g_vs, *g_fs;
typedef struct {
    int clamp, mag_point, min_filter, mip, max_anisotropy;
    float lod_bias;
    SDL_GPUSampler *sampler;
} SamplerEntry;

#define MAX_SAMPLERS 64
static SamplerEntry g_sampler[MAX_SAMPLERS];
static int g_nsamplers;

static SDL_GPUShader *load_shader(const void *code, size_t len,
                                  SDL_GPUShaderStage stage, int nsamplers,
                                  int nuniforms)
{
    SDL_GPUShaderCreateInfo ci;
    SDL_GPUShader *s;

    memset(&ci, 0, sizeof ci);
    ci.code = (const Uint8 *)code;
    ci.code_size = len;
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;
    ci.num_samplers = nsamplers;
    ci.num_uniform_buffers = nuniforms;
    s = SDL_CreateGPUShader(g_gpu, &ci);
    if (!s)
        fprintf(stderr, "gpu: SDL_CreateGPUShader failed: %s\n", SDL_GetError());
    return s;
}

static int shaders_ready(void)
{
    if (g_vs && g_fs) return 1;
    g_vs = load_shader(d3d8_fixed_vert_spv, sizeof d3d8_fixed_vert_spv,
                       SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    g_fs = load_shader(d3d8_fixed_frag_spv, sizeof d3d8_fixed_frag_spv,
                       /* TWO samplers: the 2D stage and the cube one. The
                          count here must match what the SPIR-V declares --
                          the pipeline layout is built from this number, and a
                          shader that binds more than it declared is a
                          validation error that, without a layer loaded, is
                          simply undefined texels. */
                       SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
    return g_vs && g_fs;
}

static SDL_GPUBlendFactor sdl_blend(GpuBlend b);

int gpu_blend_supported(GpuBlend b)
{
    return sdl_blend(b) != SDL_GPU_BLENDFACTOR_INVALID;
}

static SDL_GPUBlendFactor sdl_blend(GpuBlend b)
{
    switch (b) {
    case GPU_BLEND_ZERO:          return SDL_GPU_BLENDFACTOR_ZERO;
    case GPU_BLEND_ONE:           return SDL_GPU_BLENDFACTOR_ONE;
    case GPU_BLEND_SRCALPHA:      return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case GPU_BLEND_INVSRCALPHA:   return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case GPU_BLEND_SRCCOLOR:      return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case GPU_BLEND_INVSRCCOLOR:   return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case GPU_BLEND_DESTALPHA:     return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case GPU_BLEND_INVDESTALPHA:  return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case GPU_BLEND_DESTCOLOR:     return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case GPU_BLEND_INVDESTCOLOR:  return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_INVALID;
}

static SDL_GPUCompareOp sdl_compare(GpuCompare c)
{
    switch (c) {
    case GPU_CMP_NEVER:        return SDL_GPU_COMPAREOP_NEVER;
    case GPU_CMP_LESS:         return SDL_GPU_COMPAREOP_LESS;
    case GPU_CMP_EQUAL:        return SDL_GPU_COMPAREOP_EQUAL;
    case GPU_CMP_LESSEQUAL:    return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case GPU_CMP_GREATER:      return SDL_GPU_COMPAREOP_GREATER;
    case GPU_CMP_NOTEQUAL:     return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case GPU_CMP_GREATEREQUAL: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case GPU_CMP_ALWAYS:       return SDL_GPU_COMPAREOP_ALWAYS;
    }
    return SDL_GPU_COMPAREOP_INVALID;
}


typedef struct {
    PipeKey key;
    SDL_GPUGraphicsPipeline *pipe;
} PipeEntry;

#define MAX_PIPES 128
static PipeEntry g_pipes[MAX_PIPES];
static int g_npipes;
/*
 * How many were EVER built, which is not g_npipes.
 *
 * g_npipes is the live size of the cache and the teardown at the bottom of
 * this file zeroes it -- and the engine releases the device before the
 * shutdown report runs, so the report said "0 pipeline(s) built" beside half
 * a million draws. That is not a small number, it is the wrong question asked
 * after the answer was destroyed. This one only ever goes up.
 */
static unsigned long g_pipes_built;

SDL_GPUGraphicsPipeline *gpu_pipeline_for(const PipeKey *k)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;
    SDL_GPUVertexBufferDescription vb;
    SDL_GPUVertexAttribute at[5];
    int nat = 0, i;

    for (i = 0; i < g_npipes; i++)
        if (memcmp(&g_pipes[i].key, k, sizeof *k) == 0)
            return g_pipes[i].pipe;
    if (g_npipes == MAX_PIPES) {
        fprintf(stderr, "gpu: more than %d distinct pipeline states.\n",
                MAX_PIPES);
        return NULL;
    }
    if (!shaders_ready()) return NULL;

    memset(&ci, 0, sizeof ci);
    memset(&ct, 0, sizeof ct);
    memset(&vb, 0, sizeof vb);
    memset(at, 0, sizeof at);

    vb.slot = 0;
    vb.pitch = k->stride;
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    /* Position is always present; the shader needs all three attributes bound,
       so an absent colour or texcoord is pointed at the position's bytes and
       neutralised by the uniforms instead. A missing binding would be a
       validation error, and a zero stride would read the same vertex. */
    at[nat].location = 0;
    at[nat].buffer_slot = 0;
    at[nat].format = k->pos_is_float4 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
                                      : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    at[nat].offset = (Uint32)k->pos_offset;
    nat++;
    at[nat].location = 1;
    at[nat].buffer_slot = 0;
    at[nat].format = k->color_is_float4 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
                                        : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    at[nat].offset = (Uint32)(k->color_offset >= 0 ? k->color_offset
                                                   : k->pos_offset);
    nat++;
    at[nat].location = 2;
    at[nat].buffer_slot = 0;
    at[nat].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    at[nat].offset = (Uint32)(k->uv_offset >= 0 ? k->uv_offset : k->pos_offset);
    nat++;
    at[nat].location = 3;
    at[nat].buffer_slot = 0;
    at[nat].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    at[nat].offset = (Uint32)(k->normal_offset >= 0 ? k->normal_offset
                                                    : k->pos_offset);
    nat++;
    at[nat].location = 4;
    at[nat].buffer_slot = 0;
    at[nat].format = k->specular_is_float4
                         ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
                         : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    at[nat].offset = (Uint32)(k->specular_offset >= 0
                                  ? k->specular_offset : k->pos_offset);
    nat++;

    ci.vertex_input_state.vertex_buffer_descriptions = &vb;
    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_attributes = at;
    ci.vertex_input_state.num_vertex_attributes = (Uint32)nat;

    ci.vertex_shader = g_vs;
    ci.fragment_shader = g_fs;
    ci.primitive_type = k->prim == GPU_PRIM_TRIANGLESTRIP
                            ? SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP
                        : k->prim == GPU_PRIM_LINELIST
                            ? SDL_GPU_PRIMITIVETYPE_LINELIST
                            : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = k->cull == GPU_CULL_CW
                                        ? SDL_GPU_CULLMODE_BACK
                                    : k->cull == GPU_CULL_CCW
                                        ? SDL_GPU_CULLMODE_FRONT
                                        : SDL_GPU_CULLMODE_NONE;
    /*
     * COUNTER_CLOCKWISE, and it is not a guess.
     *
     * D3D evaluates winding after projection in its Y-down screen space. Both
     * the fixed-function XYZ transform and the explicit XYZRHW pixel-to-clip
     * conversion now preserve that screen-space classification, so they share
     * the same cull mapping. The two paths are independently measured by the
     * D3D8 pixel self-tests; the XYZRHW test is asymmetric so a vertical mirror
     * cannot preserve its answer merely by still covering the centre pixel.
     */
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    ct.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    ct.blend_state.enable_blend = k->blend_enable ? true : false;
    if (k->blend_enable) {
        ct.blend_state.src_color_blendfactor = sdl_blend((GpuBlend)k->src_blend);
        ct.blend_state.dst_color_blendfactor = sdl_blend((GpuBlend)k->dst_blend);
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = sdl_blend((GpuBlend)k->src_blend);
        ct.blend_state.dst_alpha_blendfactor = sdl_blend((GpuBlend)k->dst_blend);
        ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    /*
     * The pipeline must declare exactly what the pass attaches. gpu_pass_begin
     * attaches the depth target whenever one could be made, so the format is
     * asked for rather than assumed -- and when there is none, the depth state
     * is dropped rather than declared against a target that does not exist,
     * which is a validation error and not a picture.
     */
    if (gpu_depth_format() != SDL_GPU_TEXTUREFORMAT_INVALID) {
        ci.target_info.has_depth_stencil_target = true;
        ci.target_info.depth_stencil_format = gpu_depth_format();
        ci.depth_stencil_state.enable_depth_test = k->depth_test ? true : false;
        ci.depth_stencil_state.enable_depth_write = k->depth_write ? true : false;
        ci.depth_stencil_state.compare_op = sdl_compare((GpuCompare)k->depth_func);
    } else {
        ci.target_info.has_depth_stencil_target = false;
    }

    g_pipes[g_npipes].key = *k;
    g_pipes[g_npipes].pipe = SDL_CreateGPUGraphicsPipeline(g_gpu, &ci);
    if (!g_pipes[g_npipes].pipe) {
        fprintf(stderr, "gpu: SDL_CreateGPUGraphicsPipeline failed: %s\n",
                SDL_GetError());
        return NULL;
    }
    g_pipes_built++;
    return g_pipes[g_npipes++].pipe;
}

SDL_GPUSampler *gpu_sampler_for(int clamp, int mag_point, int min_filter,
                                int mip, float lod_bias, int max_anisotropy)
{
    int min_point = min_filter == 1;
    int i;
    SDL_GPUSamplerCreateInfo ci;

    if (max_anisotropy < 1) max_anisotropy = 1;
    for (i = 0; i < g_nsamplers; i++)
        if (g_sampler[i].clamp == clamp
                && g_sampler[i].mag_point == mag_point
                && g_sampler[i].min_filter == min_filter
                && g_sampler[i].mip == mip
                && g_sampler[i].lod_bias == lod_bias
                && g_sampler[i].max_anisotropy == max_anisotropy)
            return g_sampler[i].sampler;
    if (g_nsamplers == MAX_SAMPLERS) {
        fprintf(stderr, "gpu: more than %d distinct sampler states.\n",
                MAX_SAMPLERS);
        return NULL;
    }
    memset(&ci, 0, sizeof ci);
    ci.min_filter = min_point ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    ci.mag_filter = mag_point ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    ci.mipmap_mode = mip == 2 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                              : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    ci.address_mode_u = clamp ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                              : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    ci.address_mode_v = ci.address_mode_u;
    ci.address_mode_w = ci.address_mode_u;
    ci.mip_lod_bias = lod_bias;
    ci.enable_anisotropy = min_filter == 3;
    ci.max_anisotropy = (float)max_anisotropy;
    /* A zero-initialised max_lod clamps every lookup to level 0. D3D's
       MIPFILTER=POINT/LINEAR permits the complete resident chain. */
    ci.min_lod = 0.0f;
    ci.max_lod = mip ? 1000.0f : 0.0f;
    g_sampler[g_nsamplers].clamp = clamp;
    g_sampler[g_nsamplers].mag_point = mag_point;
    g_sampler[g_nsamplers].min_filter = min_filter;
    g_sampler[g_nsamplers].mip = mip;
    g_sampler[g_nsamplers].lod_bias = lod_bias;
    g_sampler[g_nsamplers].max_anisotropy = max_anisotropy;
    g_sampler[g_nsamplers].sampler = SDL_CreateGPUSampler(g_gpu, &ci);
    if (!g_sampler[g_nsamplers].sampler) {
        fprintf(stderr, "gpu: SDL_CreateGPUSampler failed: %s\n", SDL_GetError());
        return NULL;
    }
    return g_sampler[g_nsamplers++].sampler;
}


unsigned long gpu_pipelines_built(void) { return g_pipes_built; }
int gpu_pipelines_cached(void) { return g_npipes; }

void gpu_pipeline_shutdown(void)
{
    int i;
    if (!g_gpu) return;
    for (i = 0; i < g_npipes; i++)
        if (g_pipes[i].pipe) SDL_ReleaseGPUGraphicsPipeline(g_gpu, g_pipes[i].pipe);
    g_npipes = 0;
    for (i = 0; i < g_nsamplers; i++)
        if (g_sampler[i].sampler) {
            SDL_ReleaseGPUSampler(g_gpu, g_sampler[i].sampler);
            g_sampler[i].sampler = NULL;
        }
    g_nsamplers = 0;
    if (g_vs) { SDL_ReleaseGPUShader(g_gpu, g_vs); g_vs = NULL; }
    if (g_fs) { SDL_ReleaseGPUShader(g_gpu, g_fs); g_fs = NULL; }
}
#endif /* X2_WITH_SDL */
