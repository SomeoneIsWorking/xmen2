/* See gpu_draw.h. */
#include "gpu_draw.h"
#include "gpu_device.h"
#include "gpu_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef X2_WITH_SDL

/* Without SDL there is no GPU at all. Every entry point REFUSES rather than
   returning a handle that cannot be used -- a build with no graphics must not
   be able to look like one that draws nothing. */
static int no_sdl(const char *what)
{
    fprintf(stderr, "gpu: %s -- this build has no SDL, so there is no GPU.\n",
            what);
    return 0;
}
GpuBuffer  gpu_buffer_create(GpuBufferKind k, uint32_t n)
{ (void)k; (void)n; return (GpuBuffer)no_sdl("buffer create"); }
int gpu_buffer_upload(GpuBuffer b, uint32_t o, const void *d, uint32_t n)
{ (void)b; (void)o; (void)d; (void)n; return no_sdl("buffer upload"); }
void gpu_buffer_destroy(GpuBuffer b) { (void)b; }
GpuTexture gpu_texture_create(uint32_t w, uint32_t h, GpuFormat f, uint32_t l)
{ (void)w; (void)h; (void)f; (void)l; return (GpuTexture)no_sdl("texture create"); }
int gpu_texture_upload(GpuTexture t, uint32_t l, const void *d, uint32_t n)
{ (void)t; (void)l; (void)d; (void)n; return no_sdl("texture upload"); }
void gpu_texture_destroy(GpuTexture t) { (void)t; }
GpuTexture gpu_texture_create_cube(uint32_t s, GpuFormat f, uint32_t l)
{ (void)s; (void)f; (void)l; return (GpuTexture)no_sdl("cube texture create"); }
int gpu_texture_upload_face(GpuTexture t, uint32_t fc, uint32_t l,
                            const void *d, uint32_t n)
{ (void)t; (void)fc; (void)l; (void)d; (void)n;
  return no_sdl("cube texture upload"); }
int gpu_texture_is_cube(GpuTexture t) { (void)t; return 0; }
int  gpu_draw(const GpuDraw *d) { (void)d; return no_sdl("draw"); }
void gpu_draw_report(void) { }
void gpu_draw_counts(unsigned long *s2, unsigned long *r) { *s2 = *r = 0; }
int  gpu_offscreen_begin(uint32_t w, uint32_t h, float r, float g, float b,
                         float a)
{ (void)w; (void)h; (void)r; (void)g; (void)b; (void)a;
  return no_sdl("offscreen begin"); }
int  gpu_offscreen_read(void *o, uint32_t n) { (void)o; (void)n;
  return no_sdl("offscreen read"); }
void gpu_offscreen_end(void) { }

#else /* X2_WITH_SDL */

/* Compiled from src/gpu/shaders/*.{vert,frag} at build time; see CMakeLists.
   The declaration lives here so the generated file is nothing but SPIR-V. */
static const unsigned int d3d8_fixed_vert_spv[] =
#include "shaders/d3d8_fixed_vert.inc"
;
static const unsigned int d3d8_fixed_frag_spv[] =
#include "shaders/d3d8_fixed_frag.inc"
;

/* ---- resources --------------------------------------------------------- */

/*
 * Handles are indices, not pointers.
 *
 * The guest holds these indirectly (a D3D8 resource object carries one), and
 * an index into a table this file owns means a stale handle is CAUGHT -- the
 * generation counter makes a released handle report itself rather than
 * addressing whatever took its slot.
 */
typedef struct {
    SDL_GPUBuffer  *buf;
    SDL_GPUTexture *tex;
    uint32_t        bytes;
    uint32_t        w, h;
    GpuFormat       fmt;
    uint32_t        levels;
    uint32_t        faces;         /* 1 for a 2D texture, 6 for a cube */
    int             cube_refused;  /* this cube's draw refusal was reported */
    int             kind;          /* GpuBufferKind, or 0 for a texture */
    int             live;
} Res;

#define MAX_RES 4096
static Res g_res[MAX_RES];
static int g_nres;

static Res *res_get(uint32_t h, int want_texture, const char *what)
{
    Res *r;
    if (!h || h > (uint32_t)g_nres) {
        fprintf(stderr, "gpu: %s given handle %u, which was never created.\n",
                what, h);
        return NULL;
    }
    r = &g_res[h - 1];
    if (!r->live) {
        fprintf(stderr, "gpu: %s given handle %u, which has been destroyed.\n",
                what, h);
        return NULL;
    }
    if (want_texture != (r->tex != NULL)) {
        fprintf(stderr, "gpu: %s given handle %u, which is a %s.\n", what, h,
                r->tex ? "texture" : "buffer");
        return NULL;
    }
    return r;
}

static uint32_t res_alloc(void)
{
    int i;
    for (i = 0; i < g_nres; i++)
        if (!g_res[i].live) { memset(&g_res[i], 0, sizeof g_res[i]); return (uint32_t)i + 1; }
    if (g_nres == MAX_RES) {
        fprintf(stderr, "gpu: more than %d live resources.\n", MAX_RES);
        return 0;
    }
    memset(&g_res[g_nres], 0, sizeof g_res[0]);
    return (uint32_t)++g_nres;
}

GpuBuffer gpu_buffer_create(GpuBufferKind kind, uint32_t bytes)
{
    SDL_GPUBufferCreateInfo ci;
    uint32_t h;
    Res *r;

    if (!g_gpu) { fprintf(stderr, "gpu: no device; no buffer.\n"); return 0; }
    if (!bytes) {
        fprintf(stderr, "gpu: a zero-byte buffer was asked for. Refusing: the "
                        "guest's size calculation is wrong, and a zero buffer "
                        "would fail at the draw instead.\n");
        return 0;
    }
    if (!(h = res_alloc())) return 0;
    r = &g_res[h - 1];

    memset(&ci, 0, sizeof ci);
    ci.usage = (kind == GPU_BUF_INDEX) ? SDL_GPU_BUFFERUSAGE_INDEX
                                       : SDL_GPU_BUFFERUSAGE_VERTEX;
    ci.size = bytes;
    r->buf = SDL_CreateGPUBuffer(g_gpu, &ci);
    if (!r->buf) {
        fprintf(stderr, "gpu: SDL_CreateGPUBuffer(%u) failed: %s\n", bytes,
                SDL_GetError());
        return 0;
    }
    r->bytes = bytes;
    r->kind = kind;
    r->live = 1;
    return h;
}

/*
 * Upload through a transfer buffer.
 *
 * SDL_GPU has no "write straight into a GPU buffer": the data goes into a
 * mapped transfer buffer and a copy pass moves it. The transfer buffer is
 * created per upload rather than pooled -- correctness first; if this shows up
 * in a profile it is a cache, not a redesign.
 */
static int upload_bytes(SDL_GPUBuffer *dst, uint32_t offset, const void *data,
                        uint32_t bytes)
{
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTransferBufferLocation src;
    SDL_GPUBufferRegion dr;
    void *p;

    memset(&tci, 0, sizeof tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = bytes;
    tb = SDL_CreateGPUTransferBuffer(g_gpu, &tci);
    if (!tb) {
        fprintf(stderr, "gpu: transfer buffer (%u bytes) failed: %s\n", bytes,
                SDL_GetError());
        return 0;
    }
    p = SDL_MapGPUTransferBuffer(g_gpu, tb, false);
    if (!p) {
        fprintf(stderr, "gpu: mapping the transfer buffer failed: %s\n",
                SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
        return 0;
    }
    memcpy(p, data, bytes);
    SDL_UnmapGPUTransferBuffer(g_gpu, tb);

    cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    cp = SDL_BeginGPUCopyPass(cmd);
    memset(&src, 0, sizeof src);
    memset(&dr, 0, sizeof dr);
    src.transfer_buffer = tb;
    dr.buffer = dst;
    dr.offset = offset;
    dr.size = bytes;
    SDL_UploadToGPUBuffer(cp, &src, &dr, false);
    SDL_EndGPUCopyPass(cp);
    /* Not fenced. SDL_GPU executes submitted command buffers in order and
       tracks the resources they touch, so a draw submitted after this copy
       sees it. A fence here was tried while chasing a draw that turned out to
       be blue-on-blue, and it changed nothing -- so it is not carried as a
       precaution nobody can justify. */
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
    return 1;
}

int gpu_buffer_upload(GpuBuffer b, uint32_t offset, const void *data,
                      uint32_t bytes)
{
    Res *r = res_get(b, 0, "buffer upload");
    if (!r) return 0;
    if (!bytes) return 1;
    if ((uint64_t)offset + bytes > r->bytes) {
        /* Not clamped. An out-of-range upload means the guest's idea of the
           buffer's size differs from ours, and writing the part that fits
           would leave the rest of the geometry as whatever was there. */
        fprintf(stderr, "gpu: upload of %u bytes at %u is outside a %u byte "
                        "buffer. Refusing.\n", bytes, offset, r->bytes);
        return 0;
    }
    return upload_bytes(r->buf, offset, data, bytes);
}

void gpu_buffer_destroy(GpuBuffer b)
{
    Res *r = res_get(b, 0, "buffer destroy");
    if (!r) return;
    SDL_ReleaseGPUBuffer(g_gpu, r->buf);
    r->live = 0;
}

static SDL_GPUTextureFormat sdl_format(GpuFormat f)
{
    switch (f) {
    case GPU_FMT_BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case GPU_FMT_BC1:   return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case GPU_FMT_BC2:   return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case GPU_FMT_BC3:   return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

/* The 2D and the cube path differ in exactly two fields, so they share this
   and cannot drift apart in the rest. */
static GpuTexture texture_create(uint32_t w, uint32_t h, GpuFormat fmt,
                                 uint32_t levels, uint32_t faces)
{
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTextureFormat sf = sdl_format(fmt);
    uint32_t handle;
    Res *r;

    if (!g_gpu) { fprintf(stderr, "gpu: no device; no texture.\n"); return 0; }
    if (sf == SDL_GPU_TEXTUREFORMAT_INVALID) {
        fprintf(stderr, "gpu: texture format %d is not one this backend "
                        "has.\n", (int)fmt);
        return 0;
    }
    if (!w || !h) {
        fprintf(stderr, "gpu: a %ux%u texture was asked for.\n", w, h);
        return 0;
    }
    if (faces == 6 && w != h) {
        fprintf(stderr, "gpu: a %ux%u cube texture was asked for; cube faces "
                        "are square.\n", w, h);
        return 0;
    }
    if (!(handle = res_alloc())) return 0;
    r = &g_res[handle - 1];

    memset(&ci, 0, sizeof ci);
    ci.type = (faces == 6) ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_2D;
    ci.format = sf;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = faces;
    ci.num_levels = levels ? levels : 1;
    r->tex = SDL_CreateGPUTexture(g_gpu, &ci);
    if (!r->tex) {
        fprintf(stderr, "gpu: SDL_CreateGPUTexture(%ux%u, %u face(s)) failed: "
                        "%s\n", w, h, faces, SDL_GetError());
        return 0;
    }
    r->w = w;
    r->h = h;
    r->fmt = fmt;
    r->levels = ci.num_levels;
    r->faces = faces;
    r->live = 1;
    return handle;
}

GpuTexture gpu_texture_create(uint32_t w, uint32_t h, GpuFormat fmt,
                              uint32_t levels)
{
    return texture_create(w, h, fmt, levels, 1);
}

GpuTexture gpu_texture_create_cube(uint32_t size, GpuFormat fmt,
                                   uint32_t levels)
{
    return texture_create(size, size, fmt, levels, 6);
}

int gpu_texture_is_cube(GpuTexture t)
{
    if (!t || t > (uint32_t)g_nres) return 0;
    return g_res[t - 1].live && g_res[t - 1].tex && g_res[t - 1].faces == 6;
}

int gpu_texture_upload_face(GpuTexture t, uint32_t face, uint32_t level,
                            const void *data, uint32_t bytes)
{
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dr;
    Res *r = res_get(t, 1, "texture upload");
    uint32_t lw, lh;
    void *p;

    if (!r) return 0;
    if (level >= r->levels) {
        fprintf(stderr, "gpu: upload to level %u of a %u-level texture.\n",
                level, r->levels);
        return 0;
    }
    if (face >= r->faces) {
        fprintf(stderr, "gpu: upload to face %u of a texture with %u face(s).\n",
                face, r->faces);
        return 0;
    }
    lw = r->w >> level; if (!lw) lw = 1;
    lh = r->h >> level; if (!lh) lh = 1;

    memset(&tci, 0, sizeof tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = bytes;
    tb = SDL_CreateGPUTransferBuffer(g_gpu, &tci);
    if (!tb) {
        fprintf(stderr, "gpu: texture transfer buffer failed: %s\n",
                SDL_GetError());
        return 0;
    }
    p = SDL_MapGPUTransferBuffer(g_gpu, tb, false);
    if (!p) {
        SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
        return 0;
    }
    memcpy(p, data, bytes);
    SDL_UnmapGPUTransferBuffer(g_gpu, tb);

    cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    cp = SDL_BeginGPUCopyPass(cmd);
    memset(&src, 0, sizeof src);
    memset(&dr, 0, sizeof dr);
    src.transfer_buffer = tb;
    dr.texture = r->tex;
    dr.mip_level = level;
    dr.layer = face;
    dr.w = lw;
    dr.h = lh;
    dr.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dr, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
    return 1;
}

int gpu_texture_upload(GpuTexture t, uint32_t level, const void *data,
                       uint32_t bytes)
{
    return gpu_texture_upload_face(t, 0, level, data, bytes);
}

void gpu_texture_destroy(GpuTexture t)
{
    Res *r = res_get(t, 1, "texture destroy");
    if (!r) return;
    SDL_ReleaseGPUTexture(g_gpu, r->tex);
    r->live = 0;
}

/* ---- the pipeline ------------------------------------------------------ */

static SDL_GPUShader *g_vs, *g_fs;
static SDL_GPUSampler *g_sampler[4];      /* [clamp][point] */

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
                       SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    return g_vs && g_fs;
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
    int      pos_offset, color_offset, uv_offset;
    int      pos_is_float4;
    int      prim;
    int      blend_enable, src_blend, dst_blend;
    int      depth_test, depth_write, depth_func;
    int      cull;
    int      has_depth_target;
} PipeKey;

typedef struct {
    PipeKey key;
    SDL_GPUGraphicsPipeline *pipe;
} PipeEntry;

#define MAX_PIPES 128
static PipeEntry g_pipes[MAX_PIPES];
static int g_npipes;

static SDL_GPUGraphicsPipeline *pipeline_for(const PipeKey *k)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;
    SDL_GPUVertexBufferDescription vb;
    SDL_GPUVertexAttribute at[3];
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
    at[nat].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    at[nat].offset = (Uint32)(k->color_offset >= 0 ? k->color_offset
                                                   : k->pos_offset);
    nat++;
    at[nat].location = 2;
    at[nat].buffer_slot = 0;
    at[nat].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    at[nat].offset = (Uint32)(k->uv_offset >= 0 ? k->uv_offset : k->pos_offset);
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
                                        ? SDL_GPU_CULLMODE_FRONT
                                    : k->cull == GPU_CULL_CCW
                                        ? SDL_GPU_CULLMODE_BACK
                                        : SDL_GPU_CULLMODE_NONE;
    /*
     * COUNTER_CLOCKWISE, and it is not a guess.
     *
     * D3D evaluates winding in screen space with Y downward, and its default
     * D3DCULL_CCW culls the counter-clockwise ones. Vulkan classifies the SAME
     * triangle the other way round here, because the clip-space Y flip that
     * maps D3D's screen coordinates onto Vulkan's inverts the signed area. Set
     * to CLOCKWISE, a screen-clockwise triangle -- a FRONT face by D3D's
     * rule -- was culled by the default state, so the very first draw through
     * the D3D8 layer disappeared while the same geometry drew fine with
     * culling off. Measured, in the d3d8 draw self-test.
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
    /* No depth target yet: the device's automatic depth surface is not backed
       by a GPU texture. Enabling the depth test against a target that does not
       exist is a validation error, so the state is carried and IGNORED here --
       and gpu_draw says so, once, rather than letting it look honoured. */
    ci.target_info.has_depth_stencil_target = false;

    g_pipes[g_npipes].key = *k;
    g_pipes[g_npipes].pipe = SDL_CreateGPUGraphicsPipeline(g_gpu, &ci);
    if (!g_pipes[g_npipes].pipe) {
        fprintf(stderr, "gpu: SDL_CreateGPUGraphicsPipeline failed: %s\n",
                SDL_GetError());
        return NULL;
    }
    return g_pipes[g_npipes++].pipe;
}

static SDL_GPUSampler *sampler_for(int clamp, int point)
{
    int i = (clamp ? 2 : 0) | (point ? 1 : 0);
    SDL_GPUSamplerCreateInfo ci;

    if (g_sampler[i]) return g_sampler[i];
    memset(&ci, 0, sizeof ci);
    ci.min_filter = point ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    ci.mag_filter = ci.min_filter;
    ci.mipmap_mode = point ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST
                           : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    ci.address_mode_u = clamp ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                              : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    ci.address_mode_v = ci.address_mode_u;
    ci.address_mode_w = ci.address_mode_u;
    g_sampler[i] = SDL_CreateGPUSampler(g_gpu, &ci);
    if (!g_sampler[i])
        fprintf(stderr, "gpu: SDL_CreateGPUSampler failed: %s\n", SDL_GetError());
    return g_sampler[i];
}

/* ---- the draw ---------------------------------------------------------- */

typedef struct {
    float    mvp[16];
    float    viewport[4];
    uint32_t pretransformed;
    uint32_t pad[3];
} VertexUniforms;

typedef struct {
    uint32_t texture_op;
    uint32_t alpha_test;
    float    alpha_ref;
    uint32_t pad;
} PixelUniforms;

static unsigned long g_draws, g_refused, g_depth_ignored;
/* Counted separately: "the index range does not fit" is a specific
   accusation and must not be lost in the general refusal count. */
static unsigned long g_refused_index_range;

/* The 1x1 white texture an untextured draw binds. Created once, never inside
   an open render pass -- see gpu_draw. */
static GpuTexture g_white;

static uint32_t index_count(GpuPrimitive p, uint32_t prims)
{
    switch (p) {
    case GPU_PRIM_TRIANGLELIST:  return prims * 3u;
    case GPU_PRIM_TRIANGLESTRIP: return prims + 2u;
    case GPU_PRIM_LINELIST:      return prims * 2u;
    }
    return 0;
}

static int refuse(const char *why)
{
    fprintf(stderr, "gpu: draw REFUSED -- %s\n", why);
    g_refused++;
    return 0;
}

int gpu_draw(const GpuDraw *d)
{
    PipeKey key;
    SDL_GPUGraphicsPipeline *pipe;
    SDL_GPUBufferBinding vb, ib;
    SDL_GPUTextureSamplerBinding tsb;
    VertexUniforms vu;
    PixelUniforms pu;
    Res *vres, *ires = NULL, *tres = NULL;
    SDL_GPUSampler *smp;
    uint32_t n;

    if (!g_cmd) return refuse("no frame is open");
    if (!(vres = res_get(d->vertices, 0, "draw"))) { g_refused++; return 0; }
    if (!d->vertex_stride) return refuse("a vertex stride of zero");
    if (d->pos_offset < 0) return refuse("no position in the vertex format");
    if (!(n = index_count(d->prim, d->prim_count)))
        return refuse("a primitive type this backend does not have");
    if (d->indices && !(ires = res_get(d->indices, 0, "draw"))) {
        g_refused++;
        return 0;
    }
    if (d->texop != GPU_TEXOP_NONE) {
        if (!(tres = res_get(d->texture, 1, "draw"))) { g_refused++; return 0; }
        /*
         * A cube bound to the texture stage is refused, not sampled as its
         * first face.
         *
         * The fixed-function fragment shader declares a 2D sampler; there is
         * no combination of bindings that makes it sample a cube. Substituting
         * face 0 would draw the geometry with a plausible-looking wrong
         * reflection, which reads as an art bug. Once per texture, with the
         * count in the report, so a scene full of them says so once.
         */
        if (tres->faces != 1) {
            if (!tres->cube_refused) {
                fprintf(stderr, "gpu: a CUBE texture (handle %u, %ux%u) was "
                        "bound to the texture stage. This backend's "
                        "fixed-function shader samples 2D only, so the draw is "
                        "refused rather than drawn with the wrong face. Cube "
                        "sampling is not implemented.\n",
                        d->texture, tres->w, tres->h);
                tres->cube_refused = 1;
            }
            g_refused++;
            return 0;
        }
    }

    /*
     * Everything that can allocate or submit happens BEFORE the render pass
     * opens.
     *
     * Creating the placeholder texture inside an open pass submits a copy-pass
     * command buffer of its own, and the draw that triggered it was silently
     * lost -- the very first draw produced nothing while every later one
     * worked, because by then the texture existed. Nothing here is an
     * optimisation; it is the order the API requires.
     */
    if (d->texop == GPU_TEXOP_NONE && !tres) {
        if (!g_white) {
            static const uint32_t px = 0xFFFFFFFFu;
            g_white = gpu_texture_create(1, 1, GPU_FMT_BGRA8, 1);
            if (g_white) gpu_texture_upload(g_white, 0, &px, sizeof px);
        }
        if (!g_white) return refuse("no placeholder texture could be made");
        if (!(tres = res_get(g_white, 1, "draw"))) { g_refused++; return 0; }
    }

    memset(&key, 0, sizeof key);          /* padding too: the key is memcmp'd */
    key.stride = d->vertex_stride;
    key.pos_offset = d->pos_offset;
    key.pos_is_float4 = d->pretransformed ? 1 : 0;
    key.color_offset = d->color_offset;
    key.uv_offset = d->uv_offset;
    key.prim = (int)d->prim;
    key.blend_enable = d->blend_enable;
    key.src_blend = (int)d->src_blend;
    key.dst_blend = (int)d->dst_blend;
    key.depth_test = d->depth_test;
    key.depth_write = d->depth_write;
    key.depth_func = (int)d->depth_func;
    key.cull = (int)d->cull;

    if (d->blend_enable &&
        (sdl_blend(d->src_blend) == SDL_GPU_BLENDFACTOR_INVALID ||
         sdl_blend(d->dst_blend) == SDL_GPU_BLENDFACTOR_INVALID))
        return refuse("a blend factor this backend does not have");
    if (d->depth_test && !g_depth_ignored++)
        fprintf(stderr, "gpu: the depth test is requested but there is no "
                        "depth target yet, so it is IGNORED. Everything draws "
                        "in submission order. Reported once.\n");

    if (!(pipe = pipeline_for(&key))) { g_refused++; return 0; }
    if (!(smp = sampler_for(d->texture_clamp, d->texture_point))) {
        g_refused++;
        return 0;
    }

    gpu_pass_begin();
    if (!g_pass) return refuse("the render pass could not be opened");
    SDL_BindGPUGraphicsPipeline(g_pass, pipe);

    memset(&vb, 0, sizeof vb);
    vb.buffer = vres->buf;
    vb.offset = 0;
    SDL_BindGPUVertexBuffers(g_pass, 0, &vb, 1);

    memset(&vu, 0, sizeof vu);
    memcpy(vu.mvp, d->mvp, sizeof vu.mvp);
    vu.viewport[0] = 0.0f;
    vu.viewport[1] = 0.0f;
    vu.viewport[2] = (float)g_swap_w;
    vu.viewport[3] = (float)g_swap_h;
    vu.pretransformed = d->pretransformed ? 1u : 0u;
    SDL_PushGPUVertexUniformData(g_cmd, 0, &vu, sizeof vu);

    memset(&pu, 0, sizeof pu);
    pu.texture_op = (uint32_t)d->texop;
    pu.alpha_test = d->alpha_test ? 1u : 0u;
    pu.alpha_ref = d->alpha_ref;
    SDL_PushGPUFragmentUniformData(g_cmd, 0, &pu, sizeof pu);

    /* The sampler is bound even when the draw is untextured: the fragment
       shader declares one, so an unbound sampler is a validation error and,
       without a validation layer, undefined pixels. */
    memset(&tsb, 0, sizeof tsb);
    tsb.texture = tres->tex;
    tsb.sampler = smp;
    SDL_BindGPUFragmentSamplers(g_pass, 0, &tsb, 1);

    if (ires) {
        /*
         * The index range has to FIT the buffer that is bound.
         *
         * Vulkan says so, and a validation layer will say so -- but only if
         * one is loaded, and only into a log nobody reads during a 60fps run.
         * Without the layer the draw reads past the end of the buffer and the
         * result is garbage geometry, which is how issue #38's broken text
         * looked: a panel that draws perfectly and letters that do not.
         *
         * Refused, with every number that identifies the caller, rather than
         * clamped. Clamping would draw a SHORTER version of whatever the
         * engine asked for -- a subtly wrong picture that leads nowhere.
         */
        uint32_t isz = d->index_is_32bit ? 4u : 2u;
        uint64_t need = (uint64_t)(d->first_index + n) * isz;
        if (need > ires->bytes) {
            static unsigned long told;
            if (told++ < 4)
                fprintf(stderr,
                        "gpu: draw REFUSED -- the index range runs off the end "
                        "of the bound index buffer.\n"
                        "  %u index/indices of %u byte(s) from index %u needs "
                        "%llu byte(s); the buffer is %u.\n"
                        "  gpu handle %u.\n"
                        "  primitive %d, %u primitive(s), base vertex %d. "
                        "Either the engine bound the wrong buffer or this "
                        "layer sized it wrong.%s\n",
                        n, isz, d->first_index, (unsigned long long)need,
                        ires->bytes, (unsigned)d->indices, d->prim,
                        d->prim_count,
                        (int)d->base_vertex,
                        told == 4 ? " (further ones are counted only)" : "");
            g_refused++;
            g_refused_index_range++;
            return 0;
        }
        memset(&ib, 0, sizeof ib);
        ib.buffer = ires->buf;
        ib.offset = 0;
        SDL_BindGPUIndexBuffer(g_pass, &ib,
                               d->index_is_32bit ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                                                 : SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(g_pass, n, 1, d->first_index,
                                     (Sint32)d->base_vertex, 0);
    } else {
        SDL_DrawGPUPrimitives(g_pass, n, 1, d->first_vertex, 0);
    }
    g_draws++;
    return 1;
}

void gpu_draw_counts(unsigned long *submitted, unsigned long *refused)
{
    *submitted = g_draws;
    *refused = g_refused;
}

void gpu_draw_report(void)
{
    printf("  gpu: %lu draw(s) submitted, %lu refused, %d pipeline(s) built\n",
           g_draws, g_refused, g_npipes);
    if (!g_draws)
        printf("        NOTHING was drawn. Either no draw call reached this "
               "backend, or every one was refused above.\n");
    if (g_refused_index_range)
        printf("        %lu of those ran off the end of their index buffer -- "
               "see issue #38; each said which numbers did not fit.\n",
               g_refused_index_range);
    if (g_depth_ignored)
        printf("        %lu draw(s) asked for a depth test there is no target "
               "for; they drew in submission order.\n", g_depth_ignored);
}

/* ---- off-screen, for proving the path ---------------------------------- */

static SDL_GPUTexture *g_off_tex;
static uint32_t g_off_w, g_off_h;

int gpu_offscreen_begin(uint32_t w, uint32_t h, float r, float g, float b,
                        float a)
{
    SDL_GPUTextureCreateInfo ci;

    if (!g_gpu) { fprintf(stderr, "gpu: no device.\n"); return 0; }
    gpu_offscreen_end();

    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    g_off_tex = SDL_CreateGPUTexture(g_gpu, &ci);
    if (!g_off_tex) {
        fprintf(stderr, "gpu: the off-screen target could not be made: %s\n",
                SDL_GetError());
        return 0;
    }
    g_off_w = w;
    g_off_h = h;
    g_cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!g_cmd) {
        fprintf(stderr, "gpu: no command buffer: %s\n", SDL_GetError());
        return 0;
    }
    gpu_set_offscreen_target(g_off_tex, w, h);
    gpu_frame_clear(1u, r, g, b, a, 1.0f, 0);
    return 1;
}

int gpu_offscreen_read(void *out, uint32_t bytes)
{
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUFence *fence;
    uint32_t need = g_off_w * g_off_h * 4u;
    void *p;

    if (!g_off_tex) { fprintf(stderr, "gpu: no off-screen target.\n"); return 0; }
    if (bytes < need) {
        fprintf(stderr, "gpu: the readback needs %u bytes, was given %u.\n",
                need, bytes);
        return 0;
    }
    /* The draws have to have executed before they can be read back. */
    if (g_pass) { SDL_EndGPURenderPass(g_pass); g_pass = NULL; }
    if (g_cmd) {
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g_cmd);
        g_cmd = NULL;
        if (fence) {
            SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
            SDL_ReleaseGPUFence(g_gpu, fence);
        }
    }

    memset(&tci, 0, sizeof tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = need;
    tb = SDL_CreateGPUTransferBuffer(g_gpu, &tci);
    if (!tb) { fprintf(stderr, "gpu: %s\n", SDL_GetError()); return 0; }

    cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    cp = SDL_BeginGPUCopyPass(cmd);
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.texture = g_off_tex;
    src.w = g_off_w;
    src.h = g_off_h;
    src.d = 1;
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
        SDL_ReleaseGPUFence(g_gpu, fence);
    }
    p = SDL_MapGPUTransferBuffer(g_gpu, tb, false);
    if (!p) {
        fprintf(stderr, "gpu: mapping the readback failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
        return 0;
    }
    memcpy(out, p, need);
    SDL_UnmapGPUTransferBuffer(g_gpu, tb);
    SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
    return 1;
}

void gpu_offscreen_end(void)
{
    if (g_pass) { SDL_EndGPURenderPass(g_pass); g_pass = NULL; }
    if (g_cmd) { SDL_SubmitGPUCommandBuffer(g_cmd); g_cmd = NULL; }
    if (g_off_tex) {
        SDL_ReleaseGPUTexture(g_gpu, g_off_tex);
        g_off_tex = NULL;
    }
    gpu_set_offscreen_target(NULL, 0, 0);
    g_swap = NULL;
}

void gpu_draw_shutdown(void)
{
    int i;
    if (!g_gpu) return;
    gpu_offscreen_end();
    for (i = 0; i < g_npipes; i++)
        if (g_pipes[i].pipe) SDL_ReleaseGPUGraphicsPipeline(g_gpu, g_pipes[i].pipe);
    g_npipes = 0;
    for (i = 0; i < 4; i++)
        if (g_sampler[i]) { SDL_ReleaseGPUSampler(g_gpu, g_sampler[i]);
                            g_sampler[i] = NULL; }
    if (g_vs) { SDL_ReleaseGPUShader(g_gpu, g_vs); g_vs = NULL; }
    if (g_fs) { SDL_ReleaseGPUShader(g_gpu, g_fs); g_fs = NULL; }
    for (i = 0; i < g_nres; i++)
        if (g_res[i].live) {
            if (g_res[i].buf) SDL_ReleaseGPUBuffer(g_gpu, g_res[i].buf);
            if (g_res[i].tex) SDL_ReleaseGPUTexture(g_gpu, g_res[i].tex);
            g_res[i].live = 0;
        }
    g_nres = 0;
}

#endif /* X2_WITH_SDL */
