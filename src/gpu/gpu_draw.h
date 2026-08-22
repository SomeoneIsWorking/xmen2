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

/*
 * One light, as D3D8 defines it and as the vertex stage needs it.
 *
 * Eight, because that is what D3D8's minimum guarantees and what this title
 * sets; a ninth is REFUSED by name where the state is read rather than
 * silently dropped, because a missing key light is a scene that is merely
 * dark and gets blamed on the art.
 */
#define GPU_MAX_LIGHTS 8

typedef enum {
    GPU_LIGHT_POINT = 1, GPU_LIGHT_SPOT = 2, GPU_LIGHT_DIRECTIONAL = 3
} GpuLightType;

typedef struct {
    int   type;                 /* GpuLightType */
    float diffuse[4];
    float ambient[4];
    float position[3];
    float direction[3];         /* points FROM the light, as D3D defines it */
    float range;
    float atten[3];             /* constant, linear, quadratic */
} GpuLight;

/* What the one texture stage does with the vertex colour. */
typedef enum {
    GPU_TEXOP_NONE = 0,      /* untextured: the vertex colour is the result */
    GPU_TEXOP_MODULATE = 1,
    GPU_TEXOP_SELECT_TEXTURE = 2,
    /* D3DTOP_ADD. The environment map is added to the lit surface, which is
       what makes a reflection a highlight rather than a repaint. */
    GPU_TEXOP_ADD = 3,
    /* SELECTARG2. Not a curiosity: 1,070 draws a run pick arg2, and where
       arg2 is the texture factor the draw needs NO texture at all -- which is
       how a sky dome of position-only vertices gets its colour. */
    GPU_TEXOP_SELECT_ARG2 = 4
} GpuTexOp;

/*
 * Where a texture coordinate comes from when the VERTEX does not carry one.
 *
 * A cube map is addressed by a direction, and the engine's environment-mapped
 * characters use an FVF of position and normal only -- there is nothing in the
 * vertex to sample a cube with. D3D8 generates it, and D3DTSS_TEXCOORDINDEX's
 * top bits say from what. All three generators are defined in CAMERA space,
 * which is why a draw carries the world-view matrix as well as the world one.
 */
/* A combiner argument. DEFAULT keeps a zeroed draw meaning what it always
   meant; see the fields in GpuDraw. */
typedef enum {
    GPU_TA_DEFAULT = 0,
    GPU_TA_DIFFUSE = 1,
    GPU_TA_TEXTURE = 2,
    GPU_TA_TFACTOR = 3
} GpuTexArg;

typedef enum {
    GPU_TEXGEN_NONE = 0,
    GPU_TEXGEN_CAMERA_REFLECTION = 1,   /* D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR */
    GPU_TEXGEN_CAMERA_NORMAL = 2,       /* D3DTSS_TCI_CAMERASPACENORMAL */
    GPU_TEXGEN_CAMERA_POSITION = 3      /* D3DTSS_TCI_CAMERASPACEPOSITION */
} GpuTexGen;

/*
 * One draw, fully described.
 *
 * Everything the pipeline depends on is here rather than in sticky device
 * state, so a draw can be reasoned about -- and reproduced in a test -- from
 * this struct alone.
 */
typedef struct {
    GpuBuffer    vertices;
    int          owns_vertices;      /* boundary-created transient buffer */
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
    int          programmable;     /* position/color are VS 1.1 outputs */
    int          color_offset;
    int          uv_offset;
    /* The normal, which only matters when lighting is on -- but the ATTRIBUTE
       is pipeline state, so it is part of the layout either way. */
    int          normal_offset;

    float        mvp[16];          /* row-major, as D3D hands it over */
    /*
     * The WORLD matrix on its own, because D3D8 computes fixed-function
     * lighting in world space: the light positions and directions the engine
     * sets are world-space, so the vertex and its normal have to get there
     * too, and the combined mvp cannot be taken apart again.
     */
    float        world[16];

    /* ---- fixed-function lighting (D3DRS_LIGHTING) ---- */
    int          lighting;         /* 0: the vertex colour is used as-is */
    int          color_vertex;     /* D3DRS_COLORVERTEX: the vertex diffuse
                                      replaces the material's diffuse */
    int          nlights;          /* how many of `light` are enabled */
    float        global_ambient[4];
    float        mat_diffuse[4];
    float        mat_ambient[4];
    float        mat_emissive[4];
    GpuLight     light[GPU_MAX_LIGHTS];

    GpuTexture   texture;
    GpuTexOp     texop;
    /* Non-zero means the texture stage is addressed by a GENERATED direction
       rather than by the vertex's UVs. A CUBE texture with GPU_TEXGEN_NONE is
       still refused: there is no direction to sample it with, and face 0
       would be a plausible-looking wrong reflection. */
    GpuTexGen    texgen;
    float        worldview[16];
    /*
     * The combiner arguments, per D3D8's D3DTSS_COLORARG1/2 and ALPHAARG1/2:
     * GPU_TA_DEFAULT (0) means D3D8's own default for that slot -- TEXTURE for
     * ARG1, DIFFUSE for ARG2 -- so a zeroed GpuDraw still describes exactly
     * the draw it described before these fields existed. That is not a
     * convenience: every self-test and every caller that builds a draw by
     * memset would otherwise have quietly changed meaning.
     *
     * The stage used to ASSUME those defaults. 4% of this title's draws set
     * ARG2 to the texture factor, which came out as the diffuse colour.
     */
    int          color_arg1, color_arg2;   /* GpuTexArg */
    GpuTexOp     alpha_op;
    int          alpha_arg1, alpha_arg2;
    float        texture_factor[4];    /* D3DRS_TEXTUREFACTOR, RGBA 0..1 */
    int          texture_clamp;    /* else wrap */
    int          texture_point;    /* else linear */

    int          blend_enable;
    GpuBlend     src_blend, dst_blend;
    int          depth_test;
    int          depth_write;
    GpuCompare   depth_func;
    uint32_t     depth_bias;       /* Raw D3D8 D3DRS_ZBIAS value. */
    /* Raw D3D8 stencil/color-write state, retained even before the GPU path
       consumes it so diagnostics cannot call an ignored pass ordinary. */
    int          stencil_enable;
    uint32_t     stencil_fail, stencil_zfail, stencil_pass, stencil_func;
    uint32_t     stencil_ref, stencil_mask, stencil_write_mask;
    uint32_t     color_write_mask;
    GpuCull      cull;
    int          alpha_test;
    float        alpha_ref;
} GpuDraw;

/* ---- resources --------------------------------------------------------- */

GpuBuffer  gpu_buffer_create(GpuBufferKind kind, uint32_t bytes);
/* Returns 0 and reports if the range is outside the buffer -- an out-of-range
   upload is the guest telling us the size is wrong, not something to clamp.
   Uploading after a draw cycles the buffer's backing storage; only the
   uploaded range is defined in that new generation. */
int        gpu_buffer_upload(GpuBuffer b, uint32_t offset,
                             const void *data, uint32_t bytes);
void       gpu_buffer_destroy(GpuBuffer b);

GpuTexture gpu_texture_create(uint32_t w, uint32_t h, GpuFormat fmt,
                              uint32_t levels);
int        gpu_texture_upload(GpuTexture t, uint32_t level,
                              const void *data, uint32_t bytes);
void       gpu_texture_destroy(GpuTexture t);

/*
 * A cube texture: six square faces, in D3D8's face order (+X, -X, +Y, -Y,
 * +Z, -Z), which is also the layer order SDL_GPU and Vulkan use, so a face
 * index passes through untranslated.
 *
 * Sampling one is a SEPARATE question from storing one -- gpu_draw refuses a
 * cube bound to its texture stage by name, because the fixed-function shader
 * here has a 2D sampler and there is no way to sample a cube through it. See
 * gpu_draw.c.
 */
GpuTexture gpu_texture_create_cube(uint32_t size, GpuFormat fmt,
                                   uint32_t levels);
int        gpu_texture_upload_face(GpuTexture t, uint32_t face, uint32_t level,
                                   const void *data, uint32_t bytes);
/* 1 if this handle is a cube texture; 0 for a 2D one OR for a handle that is
   not a live texture at all -- callers use it to choose a message, never to
   decide the handle is valid. */
int        gpu_texture_is_cube(GpuTexture t);

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
 * The same two counts, live, for the heartbeat.
 *
 * The D3D8 layer's draw count is how many draws the ENGINE asked for; this is
 * how many reached the GPU. A run where those two disagree is drawing less
 * than it appears to, and a run where they agree and the screen is still black
 * is a different problem entirely -- the counts have to be separable to tell
 * the two apart.
 */
void gpu_draw_counts(unsigned long *submitted, unsigned long *refused);

/*
 * The frame-phase profiler's draw side: accumulated wall time inside this
 * module's two host hot paths (draw submission, transfer-buffer upload), how
 * many transfer buffers were allocated, and how many command buffers were
 * submitted from here. Live, for the heartbeat; totaled, for shutdown.
 */
void gpu_draw_perf(unsigned long long *draw_ns, unsigned long long *upload_ns,
                   unsigned long long *upload_alloc_ns,
                   unsigned long long *upload_submit_ns,
                   unsigned long long *transfer_creates, unsigned long *uploads,
                   unsigned long *submits);

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
