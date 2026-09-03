/* See gpu_draw.h. */
#include "gpu_draw.h"
#include "gpu_pipeline.h"
#include "gpu_texture_format.h"
#include "gpu_draw_trace.h"
#include "gpu_shadow.h"
#include "gpu_device.h"
#include "gpu_internal.h"
#include "gpu_upload.h"

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
void gpu_texture_request_format_support_report(void)
{ no_sdl("texture format query"); }
void gpu_texture_flush_format_support_report(void) { }
void gpu_draw_diagnostic_disable_depth(int enabled) { (void)enabled; }
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
int  gpu_offscreen_next_no_clear(void)
{ return no_sdl("offscreen next frame"); }
int  gpu_offscreen_read(void *o, uint32_t n) { (void)o; (void)n;
  return no_sdl("offscreen read"); }
void gpu_offscreen_end(void) { }

#else /* X2_WITH_SDL */

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
    GpuUploadStaging upload;
    uint32_t        bytes;
    uint32_t        w, h;
    GpuFormat       fmt;
    uint32_t        levels;
    uint32_t        faces;         /* 1 for a 2D texture, 6 for a cube */
    int             cube_refused;  /* this cube's draw refusal was reported */
    int             kind;          /* GpuBufferKind, or 0 for a texture */
    int             live;
} Res;

static unsigned long g_vs_frame = (unsigned long)-1;
static int g_diagnostic_disable_depth;

void gpu_draw_diagnostic_disable_depth(int enabled)
{
    g_diagnostic_disable_depth = enabled != 0;
    if (g_diagnostic_disable_depth)
        fprintf(stderr, "gpu: DEBUG depth comparison is disabled. This run "
                        "cannot establish correct occlusion.\n");
}

/* How many draws this frame has RECEIVED -- counted before X2_DRAW_RANGE skips
   any, so a range does not change which frames look busy. X2_SHOT_MIN_DRAWS
   reads it; see gpu_frame_draws_so_far(). */
unsigned long gpu_frame_draws_so_far(void)
{
    return gpu_draw_trace_draws_so_far();
}

/*
 * Per-frame host share, for attributing a SLOW frame at the moment it ends.
 *
 * gpu_frame_end knows the frame's wall time; these know this frame's host
 * draw/upload time so far. The two, printed together on a frame that took long
 * enough to matter, say whether the stall was guest logic (the frame crossed
 * a lot of guest code while the host share was small) or this renderer.
 * Reset once per frame by gpu_frame_begin, read by gpu_frame_end.
 */
static unsigned long long g_frame_draw_ns, g_frame_upload_ns;

/* The slow-frame attribution hooks, called by the frame owner. */
void gpu_frame_host_reset(void)
{
    g_frame_draw_ns = g_frame_upload_ns = 0;
}

void gpu_frame_host_share(unsigned long long *draw_ns,
                          unsigned long long *upload_ns)
{
    *draw_ns = g_frame_draw_ns;
    *upload_ns = g_frame_upload_ns;
}
int gpu_frame_had_programmable(void)
{
    /* gpu_frame_end increments the presented count before the capture hook. */
    return g_vs_frame + 1u == gpu_frames_presented();
}

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
 * Frame-phase profiler: accumulated wall time inside the two host hot paths
 * (draw submission, transfer-buffer upload), and how many uploads ran.
 *
 * Deliberately wall time of the HOST's share of the frame only. The guest is
 * recompiled C running on the same thread, so a frame's wall time is guest
 * crossings plus these paths plus the submit at gpu_frame_end, and the guest
 * share is what is LEFT over -- which is how these three numbers can say where
 * a slow frame actually went instead of asserting it. The reader (heartbeat)
 * takes the same torn-read trade every other counter here does.
 */
static unsigned long long g_draw_ns, g_upload_ns;
static unsigned long long g_upload_alloc_ns;   /* reserve+Map+memcpy+Unmap */
static unsigned long long g_upload_submit_ns;  /* Acquire+CopyPass+Submit+Release */
static unsigned long      g_uploads;
static unsigned long long g_transfer_creates;   /* how often the upload path allocated */
static unsigned long      g_submits;            /* SDL_SubmitGPUCommandBuffer calls */

/*
 * Upload through the destination resource's retained transfer buffer.
 *
 * SDL_GPU has no "write straight into a GPU buffer": the data goes into a
 * mapped transfer buffer and a copy pass moves it. gpu_upload_stage owns the
 * reuse/cycling rule; this owner records the copy command and its timing.
 */
static int upload_bytes(Res *r, uint32_t offset, const void *data,
                        uint32_t bytes)
{
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTransferBufferLocation src;
    SDL_GPUBufferRegion dr;
    unsigned long long t0 = gpu_perf_now_ns(), t1;
    int created;

    tb = gpu_upload_stage(g_gpu, &r->upload, r->bytes, data, bytes, &created);
    if (!tb) return 0;
    g_transfer_creates += (unsigned long long)created;
    t1 = gpu_perf_now_ns();
    g_upload_alloc_ns += t1 - t0;

    cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    cp = SDL_BeginGPUCopyPass(cmd);
    memset(&src, 0, sizeof src);
    memset(&dr, 0, sizeof dr);
    src.transfer_buffer = tb;
    dr.buffer = r->buf;
    dr.offset = offset;
    dr.size = bytes;
    /*
     * Preserve the bytes captured by draws already recorded against this
     * buffer. D3D8 dynamic buffers are commonly drawn, discarded and filled
     * again before Present; the frame command buffer is still open while this
     * copy command buffer is submitted. Without cycling, the copy overwrites
     * the backing storage both the earlier and later draws reference, so both
     * render the last actor's vertices. SDL_GPU cycling gives subsequent
     * commands a new backing allocation when the buffer is already bound.
     *
     * Bytes outside dr are undefined after a cycle. Every caller either
     * uploads the full resource or draws only the uploaded prefix.
     */
    SDL_UploadToGPUBuffer(cp, &src, &dr, true);
    SDL_EndGPUCopyPass(cp);
    /* Not fenced. SDL_GPU executes submitted command buffers in order and
       tracks the resources they touch, so a draw submitted after this copy
       sees it. A fence here was tried while chasing a draw that turned out to
       be blue-on-blue, and it changed nothing -- so it is not carried as a
       precaution nobody can justify. */
    SDL_SubmitGPUCommandBuffer(cmd);
    g_submits++;
    {
        unsigned long long now = gpu_perf_now_ns();
        g_upload_submit_ns += now - t1;
        g_frame_upload_ns += now - t0;
        g_upload_ns += now - t0;
    }
    g_uploads++;
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
    return upload_bytes(r, offset, data, bytes);
}

void gpu_buffer_destroy(GpuBuffer b)
{
    Res *r = res_get(b, 0, "buffer destroy");
    if (!r) return;
    gpu_upload_staging_destroy(g_gpu, &r->upload);
    SDL_ReleaseGPUBuffer(g_gpu, r->buf);
    r->live = 0;
}

static SDL_GPUTextureFormat sdl_format(GpuFormat f)
{
    switch (f) {
    case GPU_FMT_BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case GPU_FMT_RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case GPU_FMT_BGR8:  return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case GPU_FMT_BC1:   return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case GPU_FMT_BC2:   return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case GPU_FMT_BC3:   return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static const char *gpu_format_name(GpuFormat f)
{
    switch (f) {
    case GPU_FMT_BGRA8: return "BGRA8";
    case GPU_FMT_RGBA8: return "RGBA8";
    case GPU_FMT_BGR8: return "BGR8";
    case GPU_FMT_BC1: return "BC1/DXT1";
    case GPU_FMT_BC2: return "BC2/DXT3";
    case GPU_FMT_BC3: return "BC3/DXT5";
    }
    return "unknown";
}

static int g_format_support_report_requested;

void gpu_texture_request_format_support_report(void)
{
    static const GpuFormat formats[] = {
        GPU_FMT_BGRA8, GPU_FMT_RGBA8, GPU_FMT_BGR8,
        GPU_FMT_BC1, GPU_FMT_BC2, GPU_FMT_BC3
    };
    unsigned int i;

    g_format_support_report_requested = 1;
    if (!g_gpu) {
        return;
    }
    g_format_support_report_requested = 0;
    for (i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
        SDL_GPUTextureFormat format = sdl_format(formats[i]);
        int supported = SDL_GPUTextureSupportsFormat(
            g_gpu, format, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER) ? 1 : 0;
        fprintf(stderr, "gpu: texture format %s (%d) 2D sampler: %s\n",
                gpu_format_name(formats[i]), (int)format,
                supported ? "supported" : "UNSUPPORTED");
    }
}

void gpu_texture_flush_format_support_report(void)
{
    if (g_format_support_report_requested)
        gpu_texture_request_format_support_report();
}

static uint32_t texture_level_bytes(GpuFormat fmt, uint32_t w, uint32_t h)
{
    uint32_t blocks = ((w + 3u) / 4u) * ((h + 3u) / 4u);
    switch (fmt) {
    case GPU_FMT_BGRA8:
    case GPU_FMT_RGBA8: return w * h * 4u;
    case GPU_FMT_BGR8:  return w * h * 4u;
    case GPU_FMT_BC1:   return blocks * 8u;
    case GPU_FMT_BC2:
    case GPU_FMT_BC3:   return blocks * 16u;
    }
    return 0;
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
    r->bytes = texture_level_bytes(fmt, w, h);
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
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dr;
    Res *r = res_get(t, 1, "texture upload");
    uint32_t lw, lh;
    const void *upload_data = data;
    uint32_t upload_bytes = bytes;
    uint8_t *expanded = NULL;
    unsigned long long t0, t1;
    int created;

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
    if (r->fmt == GPU_FMT_BGR8) {
        uint32_t source_bytes = lw * lh * 3u;
        upload_bytes = lw * lh * 4u;
        if (bytes != source_bytes) {
            fprintf(stderr, "gpu: BGR8 upload is %u byte(s), expected %u for "
                            "%ux%u.\n", bytes, source_bytes, lw, lh);
            return 0;
        }
        expanded = malloc(upload_bytes);
        if (!expanded) return 0;
        gpu_bgr8_to_bgra8(data, expanded, lw * lh);
        upload_data = expanded;
    }
    t0 = gpu_perf_now_ns();

    tb = gpu_upload_stage(g_gpu, &r->upload, r->bytes, upload_data,
                          upload_bytes, &created);
    free(expanded);
    if (!tb) return 0;
    g_transfer_creates += (unsigned long long)created;
    t1 = gpu_perf_now_ns();
    g_upload_alloc_ns += t1 - t0;

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
    g_submits++;
    {
        unsigned long long now = gpu_perf_now_ns();
        g_upload_submit_ns += now - t1;
        g_frame_upload_ns += now - t0;
        g_upload_ns += now - t0;
    }
    g_uploads++;
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
    gpu_upload_staging_destroy(g_gpu, &r->upload);
    SDL_ReleaseGPUTexture(g_gpu, r->tex);
    r->live = 0;
}

/* ---- the draw ---------------------------------------------------------- */

/*
 * MUST match the std140 layout of the vertex shader's uniform block, field for
 * field -- see src/gpu/shaders/d3d8_fixed.vert. std140 aligns a mat4 and a
 * vec4 to 16 bytes, which is why the uint groups come in fours: a mismatch
 * here does not fail to compile, it silently shifts every field after it.
 */
typedef struct {
    float    mvp[16];
    float    viewport[4];
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

    float    world[16];
    float    global_ambient[4];
    float    mat_diffuse[4];
    float    mat_ambient[4];
    float    mat_emissive[4];
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
    float    worldview[16];
    uint32_t texture_transform;
    uint32_t texture_transform_pad[3];
    float    texture_matrix[16];
    uint32_t texgen1;
    uint32_t texture_transform1;
    uint32_t stage1_pad[2];
    float    texture_matrix1[16];
    /* Five vec4s per light: diffuse, ambient, (position, range),
       (direction, type), (attenuation, unused). */
    float    light[GPU_MAX_LIGHTS * 5][4];
    float    shadow_mvp[16];
    uint32_t shadow_enabled;
    uint32_t shadow_pad[3];
} VertexUniforms;

typedef struct {
    uint32_t texture_op;
    uint32_t alpha_test;
    float    alpha_ref;
    uint32_t is_cube;
    uint32_t color_arg1, color_arg2;
    uint32_t alpha_op, alpha_arg1, alpha_arg2;
    uint32_t pad[3];               /* std140: vec4 starts on a 16-byte row */
    float    tfactor[4];
    uint32_t stage1_enabled, stage1_color_op;
    uint32_t stage1_color_arg1, stage1_color_arg2;
    uint32_t stage1_alpha_op, stage1_alpha_arg1, stage1_alpha_arg2;
    uint32_t stage1_pad;
    uint32_t shadow_enabled;
    float    shadow_bias;
    float    shadow_darkness;
    float    shadow_texel_x;
    float    shadow_texel_y;
    float    shadow_pad[3];
} PixelUniforms;

/* GpuTexArg -> the shader's 0 diffuse / 1 texture / 2 factor, with
   GPU_TA_DEFAULT taking the slot's own D3D8 default. */
static uint32_t arg_of(int a, uint32_t dflt)
{
    switch (a) {
    case GPU_TA_DIFFUSE: return 0u;
    case GPU_TA_TEXTURE: return 1u;
    case GPU_TA_TFACTOR: return 2u;
    case GPU_TA_CURRENT: return 3u;
    default:             return dflt;
    }
}

static unsigned long g_draws, g_refused, g_depth_ignored;
static unsigned long g_refused_index_range;

/* The 1x1 white texture an untextured draw binds. Created once, never inside
   an open render pass -- see gpu_draw. */
static GpuTexture g_white, g_white_cube;

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
    GpuShadowSample shadow;
    Res *vres, *ires = NULL, *tres = NULL, *tres1 = NULL, *cres = NULL;
    SDL_GPUSampler *smp, *smp1;
    unsigned long long t0 = gpu_perf_now_ns();
    uint32_t n;
    int depth_test;

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
    /*
     * A combiner stage does NOT imply a bound texture.
     *
     * SELECTARG2 over the texture factor samples nothing, so demanding a real
     * handle here refused 1,043 draws a run the moment those stages stopped
     * being bypassed -- the sky dome among them, which is how a fix for a
     * white sky produced a white sky with fewer draws in it. The placeholder
     * below covers the slot the shader declares; the combiner simply does not
     * read it.
     */
    if (d->texop != GPU_TEXOP_NONE && d->texture) {
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
        if (tres->faces != 1 && d->texgen == GPU_TEXGEN_NONE) {
            /*
             * A cube with NO generated direction is still refused.
             *
             * Cube sampling itself is implemented now, but a cube is addressed
             * by a direction and this draw has none -- its texture coordinates
             * are a 2D pair, if it has any at all. Sampling face 0, or
             * building a direction out of the UVs, would draw a
             * plausible-looking wrong reflection, which reads as an art bug
             * and is exactly what this refusal exists to prevent.
             */
            if (!tres->cube_refused) {
                fprintf(stderr, "gpu: a CUBE texture (handle %u, %ux%u) was "
                        "bound with NO texture-coordinate generator, so there "
                        "is no direction to sample it with and the draw is "
                        "refused. (Cube sampling itself is implemented -- see "
                        "GpuTexGen; this is a draw that did not ask for it.)\n",
                        d->texture, tres->w, tres->h);
                tres->cube_refused = 1;
            }
            g_refused++;
            return 0;
        }
    }
    if (d->texop1 != GPU_TEXOP_NONE && d->texture1) {
        if (!(tres1 = res_get(d->texture1, 1, "draw stage 1"))) {
            g_refused++;
            return 0;
        }
        if (tres1->faces != 1)
            return refuse("texture stage 1 binds a cube; only 2D is evidenced");
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
    if (!tres) {
        if (!g_white) {
            static const uint32_t px = 0xFFFFFFFFu;
            g_white = gpu_texture_create(1, 1, GPU_FMT_BGRA8, 1);
            if (g_white) gpu_texture_upload(g_white, 0, &px, sizeof px);
        }
        if (!g_white) return refuse("no placeholder texture could be made");
        if (!(tres = res_get(g_white, 1, "draw"))) { g_refused++; return 0; }
    }
    /*
     * The shader declares BOTH a 2D sampler and a cube one, so both are bound
     * on every draw whatever it uses. An unbound sampler a shader declares is
     * a Vulkan validation error, and a build with no validation layer does not
     * fail -- it reads undefined texels, which is the kind of wrong that only
     * appears on somebody else's driver. Made here, before the pass opens, for
     * the same reason g_white is: creating a texture submits a copy pass.
     */
    if (!g_white) {
        /* Needed even by a textured draw now: a CUBE draw leaves the 2D slot
           with nothing real to bind, and the shader declares it. */
        static const uint32_t px = 0xFFFFFFFFu;
        g_white = gpu_texture_create(1, 1, GPU_FMT_BGRA8, 1);
        if (g_white) gpu_texture_upload(g_white, 0, &px, sizeof px);
    }
    if (!g_white) return refuse("no placeholder texture could be made");
    if (!g_white_cube) {
        static const uint32_t px = 0xFFFFFFFFu;
        unsigned f;
        g_white_cube = gpu_texture_create_cube(1, GPU_FMT_BGRA8, 1);
        for (f = 0; f < 6 && g_white_cube; f++)
            gpu_texture_upload_face(g_white_cube, f, 0, &px, sizeof px);
    }
    if (!g_white_cube) return refuse("no placeholder cube texture could be made");
    if (!(cres = res_get(g_white_cube, 1, "draw"))) { g_refused++; return 0; }
    if (!tres1 && !(tres1 = res_get(g_white, 1, "draw stage 1"))) {
        g_refused++;
        return 0;
    }
    /* The real cube, when this draw has one, replaces the placeholder -- and
       then the 2D slot takes the placeholder instead. */
    if (tres && tres->faces == 6) { cres = tres; tres = res_get(g_white, 1, "draw"); }

    if (d->programmable) g_vs_frame = gpu_frames_presented();
    memset(&key, 0, sizeof key);          /* padding too: the key is memcmp'd */
    key.stride = d->vertex_stride;
    key.pos_offset = d->pos_offset;
    key.pos_is_float4 = (d->pretransformed || d->programmable) ? 1 : 0;
    key.color_is_float4 = d->programmable ? 1 : 0;
    key.specular_is_float4 = d->programmable ? 1 : 0;
    key.color_offset = d->color_offset;
    key.specular_offset = d->specular_offset;
    key.uv_offset = d->uv_offset;
    key.normal_offset = d->normal_offset;
    key.prim = (int)d->prim;
    key.blend_enable = d->blend_enable;
    key.src_blend = (int)d->src_blend;
    key.dst_blend = (int)d->dst_blend;
    depth_test = d->depth_test && !g_diagnostic_disable_depth;
    key.depth_test = depth_test;
    key.depth_write = d->depth_write;
    key.depth_func = (int)d->depth_func;
    key.cull = (int)d->cull;
    key.pretransformed = d->pretransformed;

    if (d->blend_enable &&
        (!gpu_blend_supported(d->src_blend) ||
         !gpu_blend_supported(d->dst_blend)))
        return refuse("a blend factor this backend does not have");
    if (depth_test && gpu_depth_format() == SDL_GPU_TEXTUREFORMAT_INVALID &&
        !g_depth_ignored++)
        fprintf(stderr, "gpu: the depth test is requested and this device has "
                        "no depth format, so it is IGNORED. Everything draws "
                        "in submission order. Reported once.\n");

    if (!gpu_draw_trace_consider(d, gpu_frames_presented())) return 1;

    if (!(pipe = gpu_pipeline_for(&key))) { g_refused++; return 0; }
    if (!(smp = gpu_sampler_for(d->texture_clamp, d->texture_point,
                            d->texture_min_filter, d->texture_mip,
                            d->texture_lod_bias,
                            d->texture_max_anisotropy))) {
        g_refused++;
        return 0;
    }
    if (!(smp1 = gpu_sampler_for(d->texture_clamp1, d->texture_point1,
                             d->texture_min_filter1, d->texture_mip1,
                             d->texture_lod_bias1,
                             d->texture_max_anisotropy1))) {
        g_refused++;
        return 0;
    }

    if (!ires || (uint64_t)(d->first_index + n)
                     * (d->index_is_32bit ? 4u : 2u) <= ires->bytes)
        gpu_shadow_record(d, vres->buf, ires ? ires->buf : NULL,
                          tres->tex, smp, n);
    gpu_shadow_sample(d, &shadow);

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
    vu.programmable = d->programmable ? 1u : 0u;
    vu.has_diffuse = d->color_offset >= 0 ? 1u : 0u;
    vu.has_normal = d->normal_offset >= 0 ? 1u : 0u;
    vu.has_specular = d->specular_offset >= 0 ? 1u : 0u;
    vu.lighting = d->lighting ? 1u : 0u;
    vu.color_vertex = d->color_vertex ? 1u : 0u;
    vu.normalize_normals = d->normalize_normals ? 1u : 0u;
    vu.diffuse_source = d->diffuse_source;
    vu.ambient_source = d->ambient_source;
    vu.emissive_source = d->emissive_source;
    vu.texgen = (uint32_t)d->texgen;
    memcpy(vu.worldview, d->worldview, sizeof vu.worldview);
    vu.texture_transform = d->texture_transform;
    memcpy(vu.texture_matrix, d->texture_matrix, sizeof vu.texture_matrix);
    vu.texgen1 = (uint32_t)d->texgen1;
    vu.texture_transform1 = (uint32_t)d->texture_transform1;
    memcpy(vu.texture_matrix1, d->texture_matrix1,
           sizeof vu.texture_matrix1);
    if (d->lighting) {
        int li;
        memcpy(vu.world, d->world, sizeof vu.world);
        memcpy(vu.global_ambient, d->global_ambient, sizeof vu.global_ambient);
        memcpy(vu.mat_diffuse, d->mat_diffuse, sizeof vu.mat_diffuse);
        memcpy(vu.mat_ambient, d->mat_ambient, sizeof vu.mat_ambient);
        memcpy(vu.mat_emissive, d->mat_emissive, sizeof vu.mat_emissive);
        vu.nlights = (uint32_t)(d->nlights > GPU_MAX_LIGHTS ? GPU_MAX_LIGHTS
                                                            : d->nlights);
        for (li = 0; li < (int)vu.nlights; li++) {
            const GpuLight *L = &d->light[li];
            memcpy(vu.light[li * 5 + 0], L->diffuse, sizeof L->diffuse);
            memcpy(vu.light[li * 5 + 1], L->ambient, sizeof L->ambient);
            memcpy(vu.light[li * 5 + 2], L->position, 3 * sizeof(float));
            vu.light[li * 5 + 2][3] = L->range;
            memcpy(vu.light[li * 5 + 3], L->direction, 3 * sizeof(float));
            vu.light[li * 5 + 3][3] = (float)L->type;
            memcpy(vu.light[li * 5 + 4], L->atten, sizeof L->atten);
            vu.light[li * 5 + 4][3] = 0.0f;
        }
    }
    if (shadow.enabled) {
        memcpy(vu.shadow_mvp, shadow.matrix, sizeof vu.shadow_mvp);
        vu.shadow_enabled = 1;
    }
    SDL_PushGPUVertexUniformData(g_cmd, 0, &vu, sizeof vu);

    memset(&pu, 0, sizeof pu);
    pu.texture_op = (uint32_t)d->texop;
    pu.alpha_test = d->alpha_test ? 1u : 0u;
    pu.alpha_ref = d->alpha_ref;
    pu.is_cube = (d->texgen != GPU_TEXGEN_NONE
                  && d->texture && gpu_texture_is_cube(d->texture)) ? 1u : 0u;
    /* GPU_TA_DEFAULT resolves HERE, to D3D8's default for that slot, so a
       zeroed draw keeps the meaning it had before these fields existed. The
       shader sees only 0 diffuse / 1 texture / 2 factor. */
    pu.color_arg1 = arg_of(d->color_arg1, 1u);
    pu.color_arg2 = arg_of(d->color_arg2, 0u);
    pu.alpha_op   = d->alpha_op ? (uint32_t)d->alpha_op : pu.texture_op;
    pu.alpha_arg1 = arg_of(d->alpha_arg1, 1u);
    pu.alpha_arg2 = arg_of(d->alpha_arg2, 0u);
    memcpy(pu.tfactor, d->texture_factor, sizeof pu.tfactor);
    pu.stage1_enabled = d->texop1 != GPU_TEXOP_NONE;
    pu.stage1_color_op = (uint32_t)d->texop1;
    pu.stage1_color_arg1 = arg_of(d->color_arg1_1, 1u);
    pu.stage1_color_arg2 = arg_of(d->color_arg2_1, 3u);
    pu.stage1_alpha_op = d->alpha_op1 ? (uint32_t)d->alpha_op1
                                     : (uint32_t)d->texop1;
    pu.stage1_alpha_arg1 = arg_of(d->alpha_arg1_1, 1u);
    pu.stage1_alpha_arg2 = arg_of(d->alpha_arg2_1, 3u);
    pu.shadow_enabled = shadow.enabled ? 1u : 0u;
    pu.shadow_bias = shadow.depth_bias;
    pu.shadow_darkness = shadow.darkness;
    pu.shadow_texel_x = shadow.texel_size[0];
    pu.shadow_texel_y = shadow.texel_size[1];
    SDL_PushGPUFragmentUniformData(g_cmd, 0, &pu, sizeof pu);

    /* The sampler is bound even when the draw is untextured: the fragment
       shader declares one, so an unbound sampler is a validation error and,
       without a validation layer, undefined pixels. */
    {
        SDL_GPUTextureSamplerBinding tsb2[4];
        memset(tsb2, 0, sizeof tsb2);
        tsb2[0].texture = tres->tex;
        tsb2[0].sampler = smp;
        tsb2[1].texture = cres->tex;
        tsb2[1].sampler = smp;
        tsb2[2].texture = tres1->tex;
        tsb2[2].sampler = smp1;
        tsb2[3].texture = shadow.enabled ? gpu_shadow_texture() : tres->tex;
        tsb2[3].sampler = shadow.enabled ? gpu_shadow_sampler() : smp;
        SDL_BindGPUFragmentSamplers(g_pass, 0, tsb2, 4);
    }
    (void)tsb;

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
    /* Accepted draws only. A refused draw escapes the timer: it reports
       itself loudly and consumes a negligible share, so chasing timing
       through every early-return refusal would add an instrument to the very
       paths the run tells us never fire. State scores the accepted cost of a
       frame, which is the number a hotspot story has to rest on. */
    {
        unsigned long long dt = gpu_perf_now_ns() - t0;
        g_frame_draw_ns += dt;
        g_draw_ns += dt;
    }
    return 1;
}

void gpu_draw_counts(unsigned long *submitted, unsigned long *refused)
{
    *submitted = g_draws;
    *refused = g_refused;
}

/*
 * The frame-phase profiler's draw side, for the heartbeat.
 *
 * The counterpart (frame wall time, submit count) lives in gpu_device.c; the
 * two together are what attribute a frame's host cost. Deltas of these across
 * a heartbeat period are easier to trust than the run's totals, which is how
 * the heartbeat reads every counter.
 */
void gpu_draw_perf(unsigned long long *draw_ns, unsigned long long *upload_ns,
                   unsigned long long *upload_alloc_ns,
                   unsigned long long *upload_submit_ns,
                   unsigned long long *transfer_creates, unsigned long *uploads,
                   unsigned long *submits)
{
    *draw_ns = g_draw_ns;
    *upload_ns = g_upload_ns;
    *upload_alloc_ns = g_upload_alloc_ns;
    *upload_submit_ns = g_upload_submit_ns;
    *transfer_creates = g_transfer_creates;
    *uploads = g_uploads;
    *submits = g_submits;
}

void gpu_draw_report(void)
{
    printf("  gpu: %lu draw(s) submitted, %lu refused, %lu pipeline(s) built "
           "(%d still cached; the device teardown empties the cache, so these "
           "differ whenever the engine released the device first)\n",
           g_draws, g_refused, gpu_pipelines_built(),
           gpu_pipelines_cached());
    if (g_draws)
        printf("        draw submission took %.3f s; uploads took %.3f s "
               "total (%.3f alloc+copy, %.3f acquire+submit) across %lu "
               "uploads using %lu transfer-buffer alloc(s), %lu command "
               "buffers submitted\n",
               (double)g_draw_ns * 1e-9, (double)g_upload_ns * 1e-9,
               (double)g_upload_alloc_ns * 1e-9,
               (double)g_upload_submit_ns * 1e-9,
               g_uploads, (unsigned long)g_transfer_creates, g_submits);
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
    gpu_draw_trace_report();
    gpu_shadow_report();
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
    gpu_shadow_frame_begin();
    gpu_frame_clear(1u, r, g, b, a, 1.0f, 0);
    return 1;
}

int gpu_offscreen_next_no_clear(void)
{
    if (!g_gpu || !g_off_tex) {
        fprintf(stderr, "gpu: no off-screen target to continue.\n");
        return 0;
    }
    /* Execute even a clear-only first frame before replacing its command
       buffer. Queue submission order then makes it the known previous image
       for the frame that follows. */
    gpu_pass_begin();
    gpu_shadow_frame_submit();
    if (g_pass) { SDL_EndGPURenderPass(g_pass); g_pass = NULL; }
    if (g_cmd) { SDL_SubmitGPUCommandBuffer(g_cmd); g_cmd = NULL; }

    g_cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!g_cmd) {
        fprintf(stderr, "gpu: no command buffer for the next off-screen "
                        "frame: %s\n", SDL_GetError());
        return 0;
    }
    gpu_set_offscreen_target(g_off_tex, g_off_w, g_off_h);
    gpu_shadow_frame_begin();
    /* gpu_frame_begin resets this mask on the real path. This helper owns an
       already-created target, so reproduce that boundary explicitly. */
    gpu_frame_clear(0u, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0u);
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
    gpu_shadow_frame_submit();
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
    gpu_shadow_frame_submit();
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
    gpu_shadow_shutdown();
    gpu_offscreen_end();
    gpu_pipeline_shutdown();
    for (i = 0; i < g_nres; i++)
        if (g_res[i].live) {
            gpu_upload_staging_destroy(g_gpu, &g_res[i].upload);
            if (g_res[i].buf) SDL_ReleaseGPUBuffer(g_gpu, g_res[i].buf);
            if (g_res[i].tex) SDL_ReleaseGPUTexture(g_gpu, g_res[i].tex);
            g_res[i].live = 0;
        }
    g_nres = 0;
    /*
     * The placeholder texture's HANDLE is an index into the table just
     * emptied, so keeping it across a device teardown means the next device's
     * first untextured draw looks up a handle that no longer exists and is
     * refused -- "draw given handle 2, which was never created". Found by the
     * depth self-test, which is the first thing to create a second device in
     * one process; the game does the same on any Reset.
     */
    g_white = 0;
    g_white_cube = 0;
}

#endif /* X2_WITH_SDL */
