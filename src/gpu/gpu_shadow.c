#include "gpu_shadow.h"

#include "gpu_internal.h"
#include "shadow_policy.h"

#include <stdio.h>
#include <string.h>

#ifndef X2_WITH_SDL
void gpu_shadow_configure(int enabled, uint32_t resolution)
{ (void)enabled; (void)resolution; }
void gpu_shadow_frame_begin(void) { }
void gpu_shadow_record(const GpuDraw *draw, struct SDL_GPUBuffer *vertices,
                       struct SDL_GPUBuffer *indices,
                       struct SDL_GPUTexture *texture,
                       struct SDL_GPUSampler *sampler, uint32_t index_count)
{ (void)draw; (void)vertices; (void)indices; (void)texture; (void)sampler;
  (void)index_count; }
void gpu_shadow_frame_submit(void) { }
int gpu_shadow_sample(const GpuDraw *draw, GpuShadowSample *sample)
{ (void)draw; memset(sample, 0, sizeof *sample); return 0; }
struct SDL_GPUTexture *gpu_shadow_texture(void) { return NULL; }
struct SDL_GPUSampler *gpu_shadow_sampler(void) { return NULL; }
void gpu_shadow_report(void) { }
void gpu_shadow_shutdown(void) { }
#else

#include <SDL3/SDL.h>

static const uint32_t DEFAULT_RESOLUTION = 1024;
static const float SAMPLE_DEPTH_BIAS = 0.0015f;
static const float SHADOW_DARKNESS = 0.55f;

static const unsigned int shadow_depth_vert_spv[] =
#include "shaders/shadow_depth_vert.inc"
;
static const unsigned int shadow_depth_frag_spv[] =
#include "shaders/shadow_depth_frag.inc"
;

typedef struct {
    uint32_t stride;
    int pos_offset;
    int uv_offset;
    int pos_is_float4;
    int primitive;
    int cull;
} ShadowPipeKey;

typedef struct {
    ShadowPipeKey key;
    SDL_GPUGraphicsPipeline *pipeline;
} ShadowPipe;

static int g_enabled = 1;
static uint32_t g_resolution = 1024;
static SDL_GPUTexture *g_texture;
static uint32_t g_texture_resolution;
static SDL_GPUSampler *g_sampler;
static SDL_GPUTextureFormat g_format;
static SDL_GPUShader *g_vertex_shader, *g_fragment_shader;
static SDL_GPUCommandBuffer *g_shadow_command;
static SDL_GPURenderPass *g_shadow_pass;
static GpuShadowFramePolicy g_frame_policy;
static int g_frame_policy_valid;
static ShadowPipe g_pipelines[48];
static unsigned g_pipeline_count;
static unsigned long g_frames, g_frames_with_light, g_frames_submitted;
static unsigned long g_casters, g_receivers, g_programmable_casters;
static unsigned long g_programmable_receivers, g_resource_failures;

static int valid_resolution(uint32_t resolution)
{
    return resolution == 512 || resolution == 1024
           || resolution == 2048 || resolution == 4096;
}

void gpu_shadow_configure(int enabled, uint32_t resolution)
{
    g_enabled = enabled != 0;
    g_resolution = valid_resolution(resolution) ? resolution
                                                 : DEFAULT_RESOLUTION;
}

static SDL_GPUTextureFormat choose_format(void)
{
    static const SDL_GPUTextureFormat formats[] = {
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
    };
    const SDL_GPUTextureUsageFlags usage =
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    unsigned i;
    for (i = 0; i < sizeof formats / sizeof formats[0]; i++)
        if (SDL_GPUTextureSupportsFormat(g_gpu, formats[i],
                                         SDL_GPU_TEXTURETYPE_2D, usage))
            return formats[i];
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static SDL_GPUShader *load_shader(const void *code, size_t size,
                                  SDL_GPUShaderStage stage, unsigned samplers,
                                  unsigned uniforms)
{
    SDL_GPUShaderCreateInfo info;
    memset(&info, 0, sizeof info);
    info.code = (const Uint8 *)code;
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(g_gpu, &info);
}

static int resources_ready(void)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUSamplerCreateInfo sampler_info;
    int created = 0;
    if (g_texture && g_texture_resolution != g_resolution) {
        SDL_ReleaseGPUTexture(g_gpu, g_texture);
        g_texture = NULL;
    }
    if (g_texture && g_sampler && g_vertex_shader && g_fragment_shader)
        return 1;
    g_format = choose_format();
    if (g_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        fprintf(stderr, "gpu shadow: no sampleable depth-target format.\n");
        g_resource_failures++;
        return 0;
    }
    if (!g_vertex_shader)
        g_vertex_shader = load_shader(shadow_depth_vert_spv,
                                      sizeof shadow_depth_vert_spv,
                                      SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    if (!g_fragment_shader)
        g_fragment_shader = load_shader(shadow_depth_frag_spv,
                                        sizeof shadow_depth_frag_spv,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (!g_vertex_shader || !g_fragment_shader) {
        fprintf(stderr, "gpu shadow: depth shaders could not be created: %s\n",
                SDL_GetError());
        g_resource_failures++;
        return 0;
    }
    if (!g_texture) {
        memset(&texture_info, 0, sizeof texture_info);
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = g_format;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                             | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = g_resolution;
        texture_info.height = g_resolution;
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        g_texture = SDL_CreateGPUTexture(g_gpu, &texture_info);
        if (!g_texture) {
            fprintf(stderr, "gpu shadow: %ux%u depth target failed: %s\n",
                    g_resolution, g_resolution, SDL_GetError());
            g_resource_failures++;
            return 0;
        }
        g_texture_resolution = g_resolution;
        created = 1;
    }
    memset(&sampler_info, 0, sizeof sampler_info);
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    if (!g_sampler) {
        g_sampler = SDL_CreateGPUSampler(g_gpu, &sampler_info);
        created = 1;
    }
    if (!g_sampler) {
        fprintf(stderr, "gpu shadow: sampler failed: %s\n", SDL_GetError());
        g_resource_failures++;
        return 0;
    }
    if (created)
        printf("gpu shadow: enabled, %ux%u sampleable depth map; solid world "
               "packet policy, first title directional light.\n",
               g_resolution, g_resolution);
    return 1;
}

static SDL_GPUGraphicsPipeline *pipeline_for(const GpuDraw *draw)
{
    ShadowPipeKey key;
    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[2];
    unsigned i;
    memset(&key, 0, sizeof key);
    key.stride = draw->vertex_stride;
    key.pos_offset = draw->pos_offset;
    key.uv_offset = draw->uv_offset;
    key.pos_is_float4 = draw->programmable;
    key.primitive = draw->prim;
    key.cull = draw->cull;
    for (i = 0; i < g_pipeline_count; i++)
        if (memcmp(&g_pipelines[i].key, &key, sizeof key) == 0)
            return g_pipelines[i].pipeline;
    if (g_pipeline_count == sizeof g_pipelines / sizeof g_pipelines[0]) {
        fprintf(stderr, "gpu shadow: depth pipeline cache is full.\n");
        return NULL;
    }
    memset(&info, 0, sizeof info);
    memset(&vertex_buffer, 0, sizeof vertex_buffer);
    memset(attributes, 0, sizeof attributes);
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = draw->vertex_stride;
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = draw->programmable
                               ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
                               : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = (uint32_t)draw->pos_offset;
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = draw->uv_offset >= 0 ? (uint32_t)draw->uv_offset
                                                : (uint32_t)draw->pos_offset;
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 2;
    info.vertex_shader = g_vertex_shader;
    info.fragment_shader = g_fragment_shader;
    info.primitive_type = draw->prim == GPU_PRIM_TRIANGLESTRIP
                              ? SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP
                              : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = draw->cull == GPU_CULL_CW
                                          ? SDL_GPU_CULLMODE_BACK
                                      : draw->cull == GPU_CULL_CCW
                                          ? SDL_GPU_CULLMODE_FRONT
                                          : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_bias = true;
    info.rasterizer_state.depth_bias_constant_factor = 1.25f;
    info.rasterizer_state.depth_bias_slope_factor = 1.75f;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = g_format;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    g_pipelines[g_pipeline_count].key = key;
    g_pipelines[g_pipeline_count].pipeline =
        SDL_CreateGPUGraphicsPipeline(g_gpu, &info);
    if (!g_pipelines[g_pipeline_count].pipeline) {
        fprintf(stderr, "gpu shadow: depth pipeline failed: %s\n", SDL_GetError());
        return NULL;
    }
    return g_pipelines[g_pipeline_count++].pipeline;
}

void gpu_shadow_frame_begin(void)
{
    g_frame_policy_valid = 0;
    g_shadow_pass = NULL;
    g_shadow_command = NULL;
    g_frames++;
    if (!g_enabled) return;
    g_shadow_command = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!g_shadow_command) {
        fprintf(stderr, "gpu shadow: command buffer failed: %s\n", SDL_GetError());
        g_resource_failures++;
    }
}

static int begin_pass(void)
{
    SDL_GPUDepthStencilTargetInfo depth;
    if (g_shadow_pass) return 1;
    memset(&depth, 0, sizeof depth);
    depth.texture = g_texture;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    g_shadow_pass = SDL_BeginGPURenderPass(g_shadow_command, NULL, 0, &depth);
    return g_shadow_pass != NULL;
}

void gpu_shadow_record(const GpuDraw *draw, SDL_GPUBuffer *vertices,
                       SDL_GPUBuffer *indices, SDL_GPUTexture *texture,
                       SDL_GPUSampler *sampler, uint32_t index_count)
{
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUBufferBinding binding;
    SDL_GPUTextureSamplerBinding texture_binding;
    struct { uint32_t enabled; float reference; float pad[2]; } alpha;
    float matrix[16];
    unsigned roles;
    if (!g_shadow_command || !draw || !vertices) return;
    roles = gpu_shadow_draw_roles(draw);
    if (!g_frame_policy_valid && gpu_shadow_frame_policy(draw, &g_frame_policy)) {
        g_frame_policy_valid = 1;
        g_frames_with_light++;
    }
    if (!(roles & GPU_SHADOW_CASTER) || !g_frame_policy_valid) return;
    if (!resources_ready()) return;
    pipeline = pipeline_for(draw);
    if (!pipeline || !begin_pass()) {
        g_resource_failures++;
        return;
    }
    gpu_shadow_draw_matrix(&g_frame_policy, draw, matrix);
    SDL_BindGPUGraphicsPipeline(g_shadow_pass, pipeline);
    memset(&binding, 0, sizeof binding);
    binding.buffer = vertices;
    SDL_BindGPUVertexBuffers(g_shadow_pass, 0, &binding, 1);
    SDL_PushGPUVertexUniformData(g_shadow_command, 0, matrix, sizeof matrix);
    memset(&texture_binding, 0, sizeof texture_binding);
    texture_binding.texture = texture;
    texture_binding.sampler = sampler;
    SDL_BindGPUFragmentSamplers(g_shadow_pass, 0, &texture_binding, 1);
    memset(&alpha, 0, sizeof alpha);
    alpha.enabled = draw->alpha_test != 0;
    alpha.reference = draw->alpha_ref;
    SDL_PushGPUFragmentUniformData(g_shadow_command, 0, &alpha, sizeof alpha);
    if (indices) {
        binding.buffer = indices;
        SDL_BindGPUIndexBuffer(g_shadow_pass, &binding,
                               draw->index_is_32bit
                                   ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                                   : SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(g_shadow_pass, index_count, 1,
                                     draw->first_index,
                                     (int32_t)draw->base_vertex, 0);
    } else {
        SDL_DrawGPUPrimitives(g_shadow_pass, index_count, 1,
                              draw->first_vertex, 0);
    }
    g_casters++;
    if (draw->programmable) g_programmable_casters++;
}

void gpu_shadow_frame_submit(void)
{
    if (!g_shadow_command) return;
    if (g_shadow_pass) {
        SDL_EndGPURenderPass(g_shadow_pass);
        g_shadow_pass = NULL;
        if (!SDL_SubmitGPUCommandBuffer(g_shadow_command)) {
            fprintf(stderr, "gpu shadow: submission failed: %s\n", SDL_GetError());
            g_resource_failures++;
        } else
            g_frames_submitted++;
    } else {
        SDL_CancelGPUCommandBuffer(g_shadow_command);
    }
    g_shadow_command = NULL;
}

int gpu_shadow_sample(const GpuDraw *draw, GpuShadowSample *sample)
{
    memset(sample, 0, sizeof *sample);
    if (!g_enabled || !g_frame_policy_valid || !g_shadow_pass
        || !(gpu_shadow_draw_roles(draw) & GPU_SHADOW_RECEIVER))
        return 0;
    gpu_shadow_draw_matrix(&g_frame_policy, draw, sample->matrix);
    sample->enabled = 1;
    sample->texel_size[0] = sample->texel_size[1] = 1.0f / (float)g_resolution;
    sample->depth_bias = SAMPLE_DEPTH_BIAS;
    sample->darkness = SHADOW_DARKNESS;
    g_receivers++;
    if (draw->programmable) g_programmable_receivers++;
    return 1;
}

SDL_GPUTexture *gpu_shadow_texture(void) { return g_texture; }
SDL_GPUSampler *gpu_shadow_sampler(void) { return g_sampler; }

void gpu_shadow_report(void)
{
    printf("  gpu shadow: %s, %ux%u; %lu/%lu frames selected a title "
           "directional light, %lu submitted; %lu caster (%lu programmable) "
           "and %lu receiver (%lu programmable) draw(s), %lu resource/pass "
           "failure(s)\n",
           g_enabled ? "enabled" : "disabled", g_resolution, g_resolution,
           g_frames_with_light, g_frames, g_frames_submitted, g_casters,
           g_programmable_casters, g_receivers, g_programmable_receivers,
           g_resource_failures);
}

void gpu_shadow_shutdown(void)
{
    unsigned i;
    if (!g_gpu) return;
    if (g_shadow_pass) SDL_EndGPURenderPass(g_shadow_pass);
    if (g_shadow_command) SDL_CancelGPUCommandBuffer(g_shadow_command);
    g_shadow_pass = NULL;
    g_shadow_command = NULL;
    for (i = 0; i < g_pipeline_count; i++)
        SDL_ReleaseGPUGraphicsPipeline(g_gpu, g_pipelines[i].pipeline);
    g_pipeline_count = 0;
    if (g_sampler) SDL_ReleaseGPUSampler(g_gpu, g_sampler);
    if (g_texture) SDL_ReleaseGPUTexture(g_gpu, g_texture);
    if (g_vertex_shader) SDL_ReleaseGPUShader(g_gpu, g_vertex_shader);
    if (g_fragment_shader) SDL_ReleaseGPUShader(g_gpu, g_fragment_shader);
    g_sampler = NULL;
    g_texture = NULL;
    g_texture_resolution = 0;
    g_vertex_shader = g_fragment_shader = NULL;
    g_format = SDL_GPU_TEXTUREFORMAT_INVALID;
}

#endif
