/*
 * IDirect3DTexture8, IDirect3DVertexBuffer8 and IDirect3DIndexBuffer8.
 *
 * All three are the same shape: the guest creates one, LOCKS it to get a
 * pointer, writes through that pointer, and unlocks. So all three keep a
 * guest-addressable staging copy -- the guest must be able to hold the
 * pointer in 32 bits, and it writes with ordinary stores this host never sees
 * -- and UPLOAD to the GPU on unlock. There is no way to observe the writes as
 * they happen, so the unlock is the only honest moment to copy.
 *
 * That costs one copy per lock. It is not a design to be proud of and it is
 * also the only one available: a persistently-mapped GPU buffer would have to
 * live below 4 GB in the guest's address space, and SDL_GPU does not offer
 * that.
 */
#include "d3d8_resource.h"
#include "d3d8_com.h"
#include "d3d8_surface.h"
#include "d3d8_types.h"

#include "gpu_draw.h"
#include "x86rt.h"
#include "guest_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define D3DRTYPE_TEXTURE     3
#define D3DRTYPE_CUBETEXTURE 5
#define D3DRTYPE_VERTEXBUFFER 6
#define D3DRTYPE_INDEXBUFFER  7

typedef struct {
    int        is_texture;
    /* buffers */
    GpuBuffer  gbuf;
    uint32_t   bytes;
    uint32_t   fvf;              /* vertex buffers only */
    uint32_t   index_format;     /* index buffers only */
    /* textures */
    GpuTexture gtex;
    uint32_t   w, h, levels, format;
    /*
     * 1 for a 2D texture, 6 for a cube map.
     *
     * Everything below indexes a texture by ONE number, the SUB-RESOURCE
     * INDEX -- face * levels + level -- because that is what a level surface
     * carries back to us on unlock (d3d8_surface only knows "the level it was
     * made for") and because it makes the 2D case, where face is always 0,
     * literally the same code.
     */
    uint32_t   faces;
    uint32_t   chain_bytes;      /* one face's whole mip chain, in bytes */
    uint32_t   usage, pool;
    /* the staging copy the guest locks */
    uint32_t   guest_bytes;      /* guest address of the staging block */
    uint32_t   guest_size;
    uint32_t   locked_sub;
    int        locked;
    /* textures: the surface handed out for each level, made on first ask and
       then kept, because D3D8's level surfaces are persistent children -- a
       fresh object per call would let the engine compare two pointers to the
       same level and find them different. */
    D3D8Object **level_surface;
    unsigned long uploads;
    uint32_t   last_upload_level;
} Resource;

static unsigned long g_textures, g_cubetextures, g_vbuffers, g_ibuffers;

static void *guest_ptr(uint32_t a, const char *what)
{
    if (!a) {
        fprintf(stderr, "d3d8: %s was given a NULL %s\n",
                d3d8_current_method(), what);
        return NULL;
    }
    return (void *)(uintptr_t)a;
}

static Resource *res_of(D3D8Object *o) { return (Resource *)d3d8_object_ctx(o); }

/* ---- format translation ------------------------------------------------ */

/*
 * D3D format to backend format.
 *
 * Returns 0 for anything this backend does not have, and the CALLER refuses
 * with the D3D name -- rather than substituting a format that "looks close",
 * which produces a texture that samples wrong and gets blamed on the art.
 */
static GpuFormat gpu_format_of(uint32_t d3dfmt)
{
    switch (d3dfmt) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8: return GPU_FMT_BGRA8;
    case D3DFMT_DXT1:     return GPU_FMT_BC1;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:     return GPU_FMT_BC2;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:     return GPU_FMT_BC3;
    default:              return (GpuFormat)0;
    }
}

/* Bytes for one mip level, and 0 if the format's size cannot be computed. */
static uint32_t level_bytes(uint32_t fmt, uint32_t w, uint32_t h)
{
    uint32_t blocks;
    switch (fmt) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return w * h * 4u;
    case D3DFMT_DXT1:
        blocks = ((w + 3u) / 4u) * ((h + 3u) / 4u);
        return blocks * 8u;
    case D3DFMT_DXT2: case D3DFMT_DXT3:
    case D3DFMT_DXT4: case D3DFMT_DXT5:
        blocks = ((w + 3u) / 4u) * ((h + 3u) / 4u);
        return blocks * 16u;
    default:
        return 0;
    }
}

static uint32_t row_pitch(uint32_t fmt, uint32_t w)
{
    switch (fmt) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8: return w * 4u;
    case D3DFMT_DXT1:     return ((w + 3u) / 4u) * 8u;
    case D3DFMT_DXT2: case D3DFMT_DXT3:
    case D3DFMT_DXT4: case D3DFMT_DXT5:
                          return ((w + 3u) / 4u) * 16u;
    default:              return 0;
    }
}

/* ---- addressing one mip level of one face ------------------------------ */

static uint32_t level_offset(Resource *r, uint32_t level)
{
    uint32_t off = 0, lw = r->w, lh = r->h, i;
    for (i = 0; i < level; i++) {
        off += level_bytes(r->format, lw ? lw : 1, lh ? lh : 1);
        lw >>= 1; lh >>= 1;
    }
    return off;
}

/*
 * The sub-resource index -- see the comment on Resource::faces.
 *
 * sub_index turns a (face, level) pair into it; sub_offset turns it back into
 * a byte offset in the staging block. Every path below speaks sub-resource
 * indices only, so the 2D case IS the cube case with face 0.
 */
static uint32_t sub_index(Resource *r, uint32_t face, uint32_t level)
{ return face * r->levels + level; }

static uint32_t sub_count(Resource *r) { return r->levels * r->faces; }

static uint32_t sub_offset(Resource *r, uint32_t sub)
{
    return (sub / r->levels) * r->chain_bytes + level_offset(r, sub % r->levels);
}

/* The dimensions of a sub-resource's level. */
static void sub_dims(Resource *r, uint32_t sub, uint32_t *w, uint32_t *h)
{
    uint32_t level = sub % r->levels;
    *w = r->w >> level; if (!*w) *w = 1;
    *h = r->h >> level; if (!*h) *h = 1;
}

/* ---- creation ---------------------------------------------------------- */

/*
 * A 2D texture (faces == 1) or a cube map (faces == 6).
 *
 * The staging block holds the faces end to end, each one a whole mip chain, so
 * the address of a sub-resource is face * chain_bytes + level_offset. A cube's
 * six faces in one allocation is not a space trick: it means the two kinds
 * share every line of the lock, unlock and upload paths below, and a fix to
 * one cannot miss the other.
 */
static D3D8Object *texture_new(uint32_t w, uint32_t h, uint32_t levels,
                               uint32_t usage, uint32_t format, uint32_t pool,
                               uint32_t faces)
{
    GpuFormat gf = gpu_format_of(format);
    Resource *r;
    uint32_t total = 0, lw = w, lh = h, i;
    const char *who = (faces == 6) ? "CreateCubeTexture" : "CreateTexture";

    if (!gf) {
        fprintf(stderr, "d3d8: %s in format %u (0x%08x), which this "
                        "backend does not have. Refusing rather than "
                        "substituting one that samples differently.\n",
                who, format, format);
        return NULL;
    }
    if (!w || !h) {
        fprintf(stderr, "d3d8: %s was asked for a %ux%u texture.\n", who, w, h);
        return NULL;
    }
    if (!levels) {
        /* 0 means "the full chain" in D3D8. */
        levels = 1;
        while ((w >> levels) || (h >> levels)) levels++;
    }
    for (i = 0; i < levels; i++) {
        uint32_t b = level_bytes(format, lw ? lw : 1, lh ? lh : 1);
        if (!b) { fprintf(stderr, "d3d8: cannot size level %u\n", i); return NULL; }
        total += b;
        lw >>= 1; lh >>= 1;
    }
    r = (Resource *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->is_texture = 1;
    r->w = w; r->h = h; r->levels = levels; r->format = format;
    r->usage = usage; r->pool = pool; r->faces = faces;
    r->chain_bytes = total;
    total *= faces;
    r->gtex = (faces == 6) ? gpu_texture_create_cube(w, gf, levels)
                           : gpu_texture_create(w, h, gf, levels);
    if (!r->gtex) { free(r); return NULL; }
    r->guest_size = total;
    r->guest_bytes = guest_malloc(total);
    if (!r->guest_bytes) {
        fprintf(stderr, "d3d8: no guest memory for a %ux%u texture's staging "
                        "copy (%u face(s), %u bytes)\n", w, h, faces, total);
        gpu_texture_destroy(r->gtex);
        free(r);
        return NULL;
    }
    r->level_surface = (D3D8Object **)calloc(levels * faces,
                                             sizeof *r->level_surface);
    if (!r->level_surface) {
        fprintf(stderr, "d3d8: out of memory for a texture's %u level "
                        "surface slot(s)\n", levels * faces);
        guest_free(r->guest_bytes);
        gpu_texture_destroy(r->gtex);
        free(r);
        return NULL;
    }
    memset((void *)(uintptr_t)r->guest_bytes, 0, total);
    if (faces == 6) g_cubetextures++; else g_textures++;
    return d3d8_object_new(faces == 6 ? D3D8_IF_IDirect3DCubeTexture8
                                      : D3D8_IF_IDirect3DTexture8, r);
}

D3D8Object *d3d8_texture_new(uint32_t w, uint32_t h, uint32_t levels,
                             uint32_t usage, uint32_t format, uint32_t pool)
{
    return texture_new(w, h, levels, usage, format, pool, 1);
}

D3D8Object *d3d8_cubetexture_new(uint32_t size, uint32_t levels,
                                 uint32_t usage, uint32_t format, uint32_t pool)
{
    return texture_new(size, size, levels, usage, format, pool, 6);
}

static D3D8Object *buffer_new(D3D8IfaceId iface, uint32_t bytes, uint32_t fvf,
                              uint32_t index_format, uint32_t usage,
                              uint32_t pool)
{
    Resource *r;
    GpuBufferKind kind = (iface == D3D8_IF_IDirect3DIndexBuffer8)
                             ? GPU_BUF_INDEX : GPU_BUF_VERTEX;

    if (!bytes) {
        fprintf(stderr, "d3d8: a zero-length buffer was asked for.\n");
        return NULL;
    }
    r = (Resource *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->bytes = bytes;
    r->fvf = fvf;
    r->index_format = index_format;
    r->usage = usage;
    r->pool = pool;
    r->gbuf = gpu_buffer_create(kind, bytes);
    if (!r->gbuf) { free(r); return NULL; }
    r->guest_size = bytes;
    r->guest_bytes = guest_malloc(bytes);
    if (!r->guest_bytes) {
        fprintf(stderr, "d3d8: no guest memory for a %u byte buffer\n", bytes);
        gpu_buffer_destroy(r->gbuf);
        free(r);
        return NULL;
    }
    memset((void *)(uintptr_t)r->guest_bytes, 0, bytes);
    if (kind == GPU_BUF_INDEX) g_ibuffers++; else g_vbuffers++;
    return d3d8_object_new(iface, r);
}

D3D8Object *d3d8_vertexbuffer_new(uint32_t bytes, uint32_t usage, uint32_t fvf,
                                  uint32_t pool)
{
    return buffer_new(D3D8_IF_IDirect3DVertexBuffer8, bytes, fvf, 0, usage,
                      pool);
}

D3D8Object *d3d8_indexbuffer_new(uint32_t bytes, uint32_t usage,
                                 uint32_t format, uint32_t pool)
{
    return buffer_new(D3D8_IF_IDirect3DIndexBuffer8, bytes, 0, format, usage,
                      pool);
}

GpuBuffer  d3d8_resource_buffer(D3D8Object *o)  { return res_of(o)->gbuf; }
GpuTexture d3d8_resource_texture(D3D8Object *o) { return res_of(o)->gtex; }
uint32_t   d3d8_resource_fvf(D3D8Object *o)     { return res_of(o)->fvf; }
uint32_t   d3d8_resource_bytes(D3D8Object *o)   { return res_of(o)->bytes; }
/* The guest address of the buffer's own bytes. A D3D8 buffer's contents live
   in guest memory and are uploaded from there on Unlock, so index data is
   always readable on the CPU -- which is what lets an indexed TRIANGLEFAN be
   expanded rather than refused. */
uint32_t   d3d8_resource_guest_bytes(D3D8Object *o)
{ return res_of(o)->guest_bytes; }
int d3d8_resource_index_is_32bit(D3D8Object *o)
{
    /* D3DFMT_INDEX32 is 102; D3DFMT_INDEX16 is 101. */
    return res_of(o)->index_format == 102;
}

/* ---- the shared IUnknown / IDirect3DResource8 half --------------------- */

static void res_QueryInterface(D3D8Object *self, CPU *C)
{
    uint32_t ppv = d3d8_arg(C, 1);
    (void)self;
    if (ppv) WR32(ppv, 0);
    d3d8_ret(C, E_NOINTERFACE);
}
static void res_AddRef(D3D8Object *self, CPU *C)
{ d3d8_ret(C, (uint32_t)d3d8_object_addref(self)); }

static void res_destroyed(D3D8Object *o)
{
    Resource *r = res_of(o);
    uint32_t i;
    /* The level surfaces are views into guest_bytes, which is about to go
       back to the arena. Telling them so is what turns a later lock into a
       named refusal instead of a write into whatever was allocated next.
       They are not freed, for the same reason d3d8_com.c retires rather than
       frees an object: a stale guest pointer must stay diagnosable. */
    for (i = 0; r->level_surface && i < sub_count(r); i++)
        if (r->level_surface[i]) d3d8_surface_storage_gone(r->level_surface[i]);
    if (r->gtex) gpu_texture_destroy(r->gtex);
    if (r->gbuf) gpu_buffer_destroy(r->gbuf);
    if (r->guest_bytes) guest_free(r->guest_bytes);
    r->gtex = 0;
    r->gbuf = 0;
    r->guest_bytes = 0;
}

static void res_Release(D3D8Object *self, CPU *C)
{ d3d8_ret(C, (uint32_t)d3d8_object_release(self)); }

static void res_GetDevice(D3D8Object *self, CPU *C)
{
    uint32_t out = d3d8_arg(C, 0);
    (void)self;
    if (out) WR32(out, 0);
    d3d8_ret(C, D3DERR_INVALIDCALL);
}
static void res_SetPriority(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, 0); }        /* the previous priority: none */
static void res_GetPriority(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, 0); }
static void res_PreLoad(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, 0); }        /* everything is already resident */

/* ---- textures ---------------------------------------------------------- */

static void tex_GetType(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, D3DRTYPE_TEXTURE); }
static void tex_GetLevelCount(D3D8Object *self, CPU *C)
{ d3d8_ret(C, res_of(self)->levels); }
static void tex_SetLOD(D3D8Object *self, CPU *C) { (void)self; d3d8_ret(C, 0); }
static void tex_GetLOD(D3D8Object *self, CPU *C) { (void)self; d3d8_ret(C, 0); }

static void tex_GetLevelDesc(D3D8Object *self, CPU *C)
{
    Resource *r = res_of(self);
    uint32_t level = d3d8_arg(C, 0);
    uint32_t *d = (uint32_t *)guest_ptr(d3d8_arg(C, 1), "level description");
    uint32_t lw, lh;

    if (!d || level >= r->levels) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    lw = r->w >> level; if (!lw) lw = 1;
    lh = r->h >> level; if (!lh) lh = 1;
    d[0] = r->format;
    d[1] = D3DRTYPE_TEXTURE;
    d[2] = r->usage;
    d[3] = r->pool;
    d[4] = level_bytes(r->format, lw, lh);
    d[5] = 0;                                    /* D3DMULTISAMPLE_NONE */
    d[6] = lw;
    d[7] = lh;
    d3d8_ret(C, D3D_OK);
}

/* The body of both LockRects: the 2D one passes face 0. */
static void lock_sub(D3D8Object *self, CPU *C, uint32_t face, uint32_t level,
                     uint32_t lr_addr, uint32_t rect)
{
    Resource *r = res_of(self);
    uint32_t *lr = (uint32_t *)guest_ptr(lr_addr, "locked rect");
    uint32_t sub, lw, lh;

    if (!lr || level >= r->levels || face >= r->faces) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    sub = sub_index(r, face, level);
    sub_dims(r, sub, &lw, &lh);
    lr[0] = row_pitch(r->format, lw);
    lr[1] = r->guest_bytes + sub_offset(r, sub);
    if (rect) {
        /* (left, top, right, bottom); the pointer is the first pixel inside
           it and the pitch is unchanged -- the same arithmetic as
           d3d8_surface.c's LockRect, and see the comment there for why it is
           arithmetic rather than a refusal. */
        const uint32_t *q = (const uint32_t *)(uintptr_t)rect;
        uint32_t left = q[0], top = q[1], right = q[2], bottom = q[3];
        uint32_t bpp = d3d8_format_bpp(r->format);

        if (right <= left || bottom <= top || right > lw || bottom > lh) {
            fprintf(stderr, "d3d8: LockRect asked for (%u,%u)-(%u,%u) of a "
                            "%ux%u texture level, which is empty or outside "
                            "it.\n", left, top, right, bottom, lw, lh);
            d3d8_ret(C, D3DERR_INVALIDCALL);
            return;
        }
        if (bpp) {
            lr[1] += top * lr[0] + left * bpp;
        } else if ((left & 3u) || (top & 3u)) {
            fprintf(stderr, "d3d8: LockRect asked for (%u,%u)-(%u,%u) of a "
                            "block-compressed level; the origin must be on a "
                            "4x4 block boundary.\n", left, top, right, bottom);
            d3d8_ret(C, D3DERR_INVALIDCALL);
            return;
        } else {
            uint32_t blocks = (lw + 3u) / 4u;
            lr[1] += (top / 4u) * lr[0] + (left / 4u) * (lr[0] / (blocks ? blocks : 1u));
        }
    }
    r->locked = 1;
    r->locked_sub = sub;
    d3d8_ret(C, D3D_OK);
}

static void tex_LockRect(D3D8Object *self, CPU *C)
{
    lock_sub(self, C, 0, d3d8_arg(C, 0), d3d8_arg(C, 1), d3d8_arg(C, 2));
}

/*
 * IDirect3DTexture8::GetSurfaceLevel -- the level as an IDirect3DSurface8.
 *
 * The engine asks for this before it fills a texture: igDx8 loads image data
 * through a surface, not through the texture's own LockRect. So the surface
 * MUST be a view onto the texture's bytes -- a surface with a buffer of its own
 * would accept every write and upload none of them, and the texture would
 * sample as whatever it was cleared to, which reads as an art bug.
 *
 * Returned with a reference, as COM requires, and that reference is the
 * TEXTURE's (see d3d8_object_set_owner): the level cannot outlive the storage
 * it points into.
 */
static void get_surface_sub(D3D8Object *self, CPU *C, uint32_t face,
                            uint32_t level, uint32_t out)
{
    Resource *r = res_of(self);
    uint32_t sub, lw, lh;

    if (!out) {
        fprintf(stderr, "d3d8: the surface for face %u level %u was asked for "
                        "with nowhere to put it.\n", face, level);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    if (level >= r->levels || face >= r->faces) {
        fprintf(stderr, "d3d8: face %u level %u of a texture with %u face(s) "
                        "and %u level(s).\n", face, level, r->faces, r->levels);
        WR32(out, 0);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    sub = sub_index(r, face, level);
    if (!r->level_surface[sub]) {
        sub_dims(r, sub, &lw, &lh);
        r->level_surface[sub] = d3d8_surface_new_texlevel(
            self, sub, lw, lh, r->format, r->usage, r->pool,
            row_pitch(r->format, lw), level_bytes(r->format, lw, lh),
            r->guest_bytes + sub_offset(r, sub));
        if (!r->level_surface[sub]) {
            WR32(out, 0);
            d3d8_ret(C, D3DERR_INVALIDCALL);
            return;
        }
    }
    d3d8_object_addref(self);            /* the one the caller will Release */
    WR32(out, d3d8_object_guest(r->level_surface[sub]));
    d3d8_ret(C, D3D_OK);
}

static void tex_GetSurfaceLevel(D3D8Object *self, CPU *C)
{
    get_surface_sub(self, C, 0, d3d8_arg(C, 0), d3d8_arg(C, 1));
}

void d3d8_texture_level_unlocked(D3D8Object *tex, uint32_t sub)
{
    Resource *r = res_of(tex);
    uint32_t lw, lh;

    if (sub >= sub_count(r)) {
        fprintf(stderr, "d3d8: an unlock of texture sub-resource %u, which has "
                        "only %u (%u face(s) x %u level(s)). Nothing was "
                        "uploaded.\n",
                sub, sub_count(r), r->faces, r->levels);
        return;
    }
    sub_dims(r, sub, &lw, &lh);
    /* The unlock is the only moment the guest's writes are known to be
       finished, so it is where the upload happens. */
    if (!gpu_texture_upload_face(r->gtex, sub / r->levels, sub % r->levels,
                                 (const void *)(uintptr_t)(r->guest_bytes +
                                                           sub_offset(r, sub)),
                                 level_bytes(r->format, lw, lh))) {
        fprintf(stderr, "d3d8: the backend REFUSED the upload of texture face "
                        "%u level %u (%ux%u). That level will sample as "
                        "whatever it held before.\n",
                sub / r->levels, sub % r->levels, lw, lh);
        return;
    }
    r->uploads++;
    r->last_upload_level = sub % r->levels;
}

unsigned long d3d8_texture_uploads(D3D8Object *o) { return res_of(o)->uploads; }
uint32_t d3d8_texture_last_upload_level(D3D8Object *o)
{ return res_of(o)->last_upload_level; }

static void unlock_sub(D3D8Object *self, CPU *C, uint32_t face, uint32_t level)
{
    Resource *r = res_of(self);

    if (!r->locked) {
        fprintf(stderr, "d3d8: UnlockRect on a texture that was not locked\n");
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    if (level >= r->levels || face >= r->faces) {
        fprintf(stderr, "d3d8: UnlockRect of face %u level %u on a texture "
                        "with %u face(s) and %u level(s).\n",
                face, level, r->faces, r->levels);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    d3d8_texture_level_unlocked(self, sub_index(r, face, level));
    r->locked = 0;
    d3d8_ret(C, D3D_OK);
}

static void tex_UnlockRect(D3D8Object *self, CPU *C)
{
    unlock_sub(self, C, 0, d3d8_arg(C, 0));
}

static void tex_AddDirtyRect(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, D3D_OK); }   /* nothing is deferred, so nothing dirties */

/* ---- cube maps --------------------------------------------------------- */

/*
 * IDirect3DCubeTexture8 is IDirect3DTexture8 with a face index in front of
 * every level index, so all of it delegates to the shared bodies above. The
 * face order is D3D8's D3DCUBEMAP_FACES enumeration -- +X, -X, +Y, -Y, +Z, -Z
 * as 0..5 -- which is the layer order the backend wants, so the number is
 * passed through rather than remapped.
 */
static void cube_GetType(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, D3DRTYPE_CUBETEXTURE); }

static void cube_GetLevelDesc(D3D8Object *self, CPU *C)
{
    Resource *r = res_of(self);
    uint32_t level = d3d8_arg(C, 0);
    uint32_t *d = (uint32_t *)guest_ptr(d3d8_arg(C, 1), "level description");
    uint32_t lw, lh;

    if (!d || level >= r->levels) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    sub_dims(r, level, &lw, &lh);
    d[0] = r->format;
    d[1] = D3DRTYPE_CUBETEXTURE;
    d[2] = r->usage;
    d[3] = r->pool;
    d[4] = level_bytes(r->format, lw, lh);
    d[5] = 0;                                    /* D3DMULTISAMPLE_NONE */
    d[6] = lw;
    d[7] = lh;
    d3d8_ret(C, D3D_OK);
}

static void cube_GetCubeMapSurface(D3D8Object *self, CPU *C)
{
    get_surface_sub(self, C, d3d8_arg(C, 0), d3d8_arg(C, 1), d3d8_arg(C, 2));
}

static void cube_LockRect(D3D8Object *self, CPU *C)
{
    lock_sub(self, C, d3d8_arg(C, 0), d3d8_arg(C, 1), d3d8_arg(C, 2),
             d3d8_arg(C, 3));
}

static void cube_UnlockRect(D3D8Object *self, CPU *C)
{
    unlock_sub(self, C, d3d8_arg(C, 0), d3d8_arg(C, 1));
}

static const D3D8MethodFn g_tex_impl[] = {
    res_QueryInterface, res_AddRef, res_Release, res_GetDevice,
    NULL, NULL, NULL,                       /* 4-6 private data */
    res_SetPriority, res_GetPriority, res_PreLoad, tex_GetType,
    tex_SetLOD, tex_GetLOD, tex_GetLevelCount,
    tex_GetLevelDesc,
    tex_GetSurfaceLevel,
    tex_LockRect, tex_UnlockRect, tex_AddDirtyRect
};

static const D3D8MethodFn g_cube_impl[] = {
    res_QueryInterface, res_AddRef, res_Release, res_GetDevice,
    NULL, NULL, NULL,                       /* 4-6 private data */
    res_SetPriority, res_GetPriority, res_PreLoad, cube_GetType,
    tex_SetLOD, tex_GetLOD, tex_GetLevelCount,
    cube_GetLevelDesc,
    cube_GetCubeMapSurface,
    cube_LockRect, cube_UnlockRect, tex_AddDirtyRect
};

/* ---- vertex and index buffers ------------------------------------------ */

static void buf_Lock(D3D8Object *self, CPU *C)
{
    Resource *r = res_of(self);
    uint32_t offset = d3d8_arg(C, 0), size = d3d8_arg(C, 1);
    uint32_t out = d3d8_arg(C, 2);

    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (!size) size = r->bytes - offset;      /* 0 means "to the end" */
    if ((uint64_t)offset + size > r->bytes) {
        fprintf(stderr, "d3d8: Lock(%u, %u) is outside a %u byte buffer.\n",
                offset, size, r->bytes);
        WR32(out, 0);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    r->locked = 1;
    WR32(out, r->guest_bytes + offset);
    d3d8_ret(C, D3D_OK);
}

static void buf_Unlock(D3D8Object *self, CPU *C)
{
    Resource *r = res_of(self);
    if (!r->locked) {
        fprintf(stderr, "d3d8: Unlock on a buffer that was not locked\n");
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    /*
     * The WHOLE buffer is uploaded, not just the locked range.
     *
     * Lock records no range because D3D8's discard/nooverwrite flags mean the
     * guest may legitimately write outside what it asked for, and uploading a
     * subrange it did not write is harmless while missing one it did write is
     * geometry that silently does not move.
     */
    gpu_buffer_upload(r->gbuf, 0, (const void *)(uintptr_t)r->guest_bytes,
                      r->bytes);
    r->locked = 0;
    d3d8_ret(C, D3D_OK);
}

static void vbuf_GetType(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, D3DRTYPE_VERTEXBUFFER); }
static void ibuf_GetType(D3D8Object *self, CPU *C)
{ (void)self; d3d8_ret(C, D3DRTYPE_INDEXBUFFER); }

static void vbuf_GetDesc(D3D8Object *self, CPU *C)
{
    Resource *r = res_of(self);
    uint32_t *d = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "description");
    if (!d) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d[0] = 0;                                  /* Format: D3DFMT_VERTEXDATA */
    d[1] = D3DRTYPE_VERTEXBUFFER;
    d[2] = r->usage;
    d[3] = r->pool;
    d[4] = r->bytes;
    d[5] = r->fvf;
    d3d8_ret(C, D3D_OK);
}

static void ibuf_GetDesc(D3D8Object *self, CPU *C)
{
    Resource *r = res_of(self);
    uint32_t *d = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "description");
    if (!d) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d[0] = r->index_format;
    d[1] = D3DRTYPE_INDEXBUFFER;
    d[2] = r->usage;
    d[3] = r->pool;
    d[4] = r->bytes;
    d3d8_ret(C, D3D_OK);
}

static const D3D8MethodFn g_vbuf_impl[] = {
    res_QueryInterface, res_AddRef, res_Release, res_GetDevice,
    NULL, NULL, NULL,
    res_SetPriority, res_GetPriority, res_PreLoad, vbuf_GetType,
    buf_Lock, buf_Unlock, vbuf_GetDesc
};

static const D3D8MethodFn g_ibuf_impl[] = {
    res_QueryInterface, res_AddRef, res_Release, res_GetDevice,
    NULL, NULL, NULL,
    res_SetPriority, res_GetPriority, res_PreLoad, ibuf_GetType,
    buf_Lock, buf_Unlock, ibuf_GetDesc
};

void d3d8_resource_install(void)
{
    d3d8_iface_implement(D3D8_IF_IDirect3DTexture8, g_tex_impl,
                         (int)(sizeof g_tex_impl / sizeof g_tex_impl[0]));
    d3d8_iface_implement(D3D8_IF_IDirect3DCubeTexture8, g_cube_impl,
                         (int)(sizeof g_cube_impl / sizeof g_cube_impl[0]));
    d3d8_iface_implement(D3D8_IF_IDirect3DVertexBuffer8, g_vbuf_impl,
                         (int)(sizeof g_vbuf_impl / sizeof g_vbuf_impl[0]));
    d3d8_iface_implement(D3D8_IF_IDirect3DIndexBuffer8, g_ibuf_impl,
                         (int)(sizeof g_ibuf_impl / sizeof g_ibuf_impl[0]));
}

void d3d8_resource_attach_destructor(D3D8Object *o)
{
    d3d8_object_set_destructor(o, res_destroyed);
}

void d3d8_resource_report(void)
{
    printf("  d3d8 resources: %lu texture(s), %lu cube texture(s), %lu vertex "
           "buffer(s), %lu index buffer(s)\n",
           g_textures, g_cubetextures, g_vbuffers, g_ibuffers);
}
