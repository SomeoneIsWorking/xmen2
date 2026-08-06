/*
 * Drawing: buffers, textures, and one draw call.
 *
 * Same rule as gpu_device.h -- this knows NOTHING about the guest. No CPU
 * state, no guest addresses, no D3D8 interfaces. The caller resolves the
 * engine's state into the plain description below and calls in with it, which
 * is what lets the whole draw path be driven, and proved, with no game running
 * (see gpu_selftest.c).
 *
 * What it is NOT: a D3D8 emulator. It offers the pipeline a fixed-function
 * title actually asks for -- a vertex format, a transform, one texture stage,
 * blend/depth/cull -- and REFUSES anything outside that rather than
 * approximating it. A silently approximated combiner reads as a lighting bug
 * and gets attributed to anything but the shader.
 */
#ifndef GPU_DRAW_H
#define GPU_DRAW_H

#include <stdint.h>

/* Opaque handles. 0 is "none" everywhere, so a zeroed description is a valid
   "nothing bound" rather than a dangling reference. */
typedef uint32_t GpuBuffer;
typedef uint32_t GpuTexture;

typedef enum {
    GPU_BUF_VERTEX = 1,
    GPU_BUF_INDEX
} GpuBufferKind;

/*
 * Pixel formats, named for what they are rather than by D3D's numbering: the
 * translation from D3DFMT_* happens at the boundary, so an unsupported format
 * is refused there with the D3D name the engine used.
 */
typedef enum {
    GPU_FMT_BGRA8 = 1,     /* D3DFMT_A8R8G8B8 / X8R8G8B8 */
    GPU_FMT_BC1,           /* DXT1 */
    GPU_FMT_BC2,           /* DXT3 */
    GPU_FMT_BC3            /* DXT5 */
} GpuFormat;

typedef enum {
    GPU_PRIM_TRIANGLELIST = 1,
    GPU_PRIM_TRIANGLESTRIP,
    GPU_PRIM_LINELIST
} GpuPrimitive;

/* D3DBLEND, translated at the boundary. */
typedef enum {
    GPU_BLEND_ZERO = 1, GPU_BLEND_ONE, GPU_BLEND_SRCALPHA,
    GPU_BLEND_INVSRCALPHA, GPU_BLEND_SRCCOLOR, GPU_BLEND_INVSRCCOLOR,
    GPU_BLEND_DESTALPHA, GPU_BLEND_INVDESTALPHA, GPU_BLEND_DESTCOLOR,
    GPU_BLEND_INVDESTCOLOR
} GpuBlend;

typedef enum { GPU_CULL_NONE = 1, GPU_CULL_CW, GPU_CULL_CCW } GpuCull;

typedef enum {
    GPU_CMP_NEVER = 1, GPU_CMP_LESS, GPU_CMP_EQUAL, GPU_CMP_LESSEQUAL,
    GPU_CMP_GREATER, GPU_CMP_NOTEQUAL, GPU_CMP_GREATEREQUAL, GPU_CMP_ALWAYS
} GpuCompare;

/* What the one texture stage does with the vertex colour. */
typedef enum {
    GPU_TEXOP_NONE = 0,      /* untextured: the vertex colour is the result */
    GPU_TEXOP_MODULATE = 1,
    GPU_TEXOP_SELECT_TEXTURE = 2
} GpuTexOp;

/*
 * One draw, fully described.
 *
 * Everything the pipeline depends on is here rather than in sticky device
 * state, so a draw can be reasoned about -- and reproduced in a test -- from
 * this struct alone.
 */
typedef struct {
    GpuBuffer    vertices;
    uint32_t     vertex_stride;
    uint32_t     first_vertex;
    GpuBuffer    indices;          /* 0 for non-indexed */
    int          index_is_32bit;
    uint32_t     first_index;
    uint32_t     base_vertex;

    GpuPrimitive prim;
    uint32_t     prim_count;

    /* Vertex layout. Offsets are byte offsets into a vertex; -1 means the
       component is absent. Position is required. */
    int          pos_offset;
    /*
     * D3DFVF_XYZRHW: the position is four floats already in screen space.
     * D3DFVF_XYZ is three, and the difference is not cosmetic -- binding four
     * where the vertex has three reads the next field as W.
     */
    int          pretransformed;
    int          color_offset;
    int          uv_offset;

    float        mvp[16];          /* row-major, as D3D hands it over */

    GpuTexture   texture;
    GpuTexOp     texop;
    int          texture_clamp;    /* else wrap */
    int          texture_point;    /* else linear */

    int          blend_enable;
    GpuBlend     src_blend, dst_blend;
    int          depth_test;
    int          depth_write;
    GpuCompare   depth_func;
    GpuCull      cull;
    int          alpha_test;
    float        alpha_ref;
} GpuDraw;

/* ---- resources --------------------------------------------------------- */

GpuBuffer  gpu_buffer_create(GpuBufferKind kind, uint32_t bytes);
/* Returns 0 and reports if the range is outside the buffer -- an out-of-range
   upload is the guest telling us the size is wrong, not something to clamp. */
int        gpu_buffer_upload(GpuBuffer b, uint32_t offset,
                             const void *data, uint32_t bytes);
void       gpu_buffer_destroy(GpuBuffer b);

GpuTexture gpu_texture_create(uint32_t w, uint32_t h, GpuFormat fmt,
                              uint32_t levels);
int        gpu_texture_upload(GpuTexture t, uint32_t level,
                              const void *data, uint32_t bytes);
void       gpu_texture_destroy(GpuTexture t);

/* ---- drawing ----------------------------------------------------------- */

/*
 * Submit one draw into the frame opened by gpu_frame_begin.
 *
 * Returns 0 and says why if it cannot: no frame open, no vertex buffer, a
 * state combination this pipeline does not implement. A draw that silently
 * does nothing is the single hardest rendering bug to find, so there is no
 * path here that returns success without drawing.
 */
int gpu_draw(const GpuDraw *d);

/* How many draws were submitted, and how many were refused and why. Printed
   at shutdown by gpu_device_report. */
void gpu_draw_report(void);

/*
 * Render into an off-screen target instead of the swapchain, and read it back.
 *
 * This exists so the draw path can be PROVED rather than asserted: the
 * self-test draws a triangle into one of these and checks the pixels. It is
 * also what the engine's own off-screen render destinations will need.
 *
 * gpu_offscreen_begin opens a frame targeting a fresh texture; _read copies it
 * back to host memory as BGRA8; _end finishes. Returns 0 on failure, loudly.
 */
int  gpu_offscreen_begin(uint32_t w, uint32_t h, float r, float g, float b,
                         float a);
int  gpu_offscreen_read(void *bgra_out, uint32_t bytes);
void gpu_offscreen_end(void);

#endif /* GPU_DRAW_H */
