/* See d3d8_surface.h. */
#include "d3d8_surface.h"
#include "d3d8_types.h"

#include "x86rt.h"
#include "guest_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define D3DRTYPE_SURFACE 1

static int g_count[4];
static const char *const KIND[] = { "back buffer", "depth/stencil",
                                    "render target", "system memory" };

uint32_t d3d8_format_bpp(uint32_t format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
    case D3DFMT_D32: case D3DFMT_D24S8: case D3DFMT_D24X8:
        return 4;
    case D3DFMT_R8G8B8:
        return 3;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_D16: case D3DFMT_D15S1:
        return 2;
    case D3DFMT_A8:
        return 1;
    default:
        /* Block-compressed formats have no bytes per PIXEL, and every other
           format is one this host has not been taught. Both must be refused
           where they are used: a default of 4 would size a buffer wrongly and
           the corruption would appear in whatever was next in memory. */
        return 0;
    }
}

static void *guest_ptr(uint32_t a, const char *what)
{
    if (!a) {
        fprintf(stderr, "d3d8: %s was given a NULL %s\n",
                d3d8_current_method(), what);
        return NULL;
    }
    return (void *)(uintptr_t)a;
}

D3D8Surface *d3d8_surface_of(D3D8Object *o)
{
    return (D3D8Surface *)d3d8_object_ctx(o);
}

D3D8Object *d3d8_surface_new(D3D8SurfaceKind kind, uint32_t w, uint32_t h,
                             uint32_t format, uint32_t usage, uint32_t pool)
{
    D3D8Surface *s = (D3D8Surface *)calloc(1, sizeof *s);
    uint32_t bpp = d3d8_format_bpp(format);

    if (!s) { fprintf(stderr, "d3d8: out of memory\n"); abort(); }
    s->kind = kind;
    s->width = w;
    s->height = h;
    s->format = format;
    s->usage = usage;
    s->pool = pool;
    s->bytes_per_pixel = bpp;
    s->pitch = w * bpp;

    if (kind == D3D8_SURF_SYSTEM) {
        if (!bpp) {
            fprintf(stderr, "d3d8: a system surface was asked for in format "
                            "%u, whose pixel size this host does not know. "
                            "Refusing rather than guessing a stride.\n", format);
            free(s);
            return NULL;
        }
        /*
         * Guest-addressable, because LockRect hands the pointer to the guest
         * and the guest writes through it. Host malloc would be above 4 GB
         * and the guest's 32-bit pointer could not hold it.
         */
        s->guest_pixels = guest_malloc(s->pitch * h);
        if (!s->guest_pixels) {
            fprintf(stderr, "d3d8: no guest memory for a %ux%u surface\n", w, h);
            free(s);
            return NULL;
        }
        s->pixels = (unsigned char *)(uintptr_t)s->guest_pixels;
        memset(s->pixels, 0, s->pitch * h);
    }
    g_count[kind]++;
    return d3d8_object_new(D3D8_IF_IDirect3DSurface8, s);
}

/* ---- the interface ----------------------------------------------------- */

static void surf_QueryInterface(D3D8Object *self, CPU *C)
{
    uint32_t ppv = d3d8_arg(C, 1);
    (void)self;
    if (ppv) WR32(ppv, 0);
    d3d8_ret(C, E_NOINTERFACE);
}

/*
 * Reference counting is per-object and lives in d3d8_com.c, because every
 * interface needs exactly this and a per-file counter is how one of them ends
 * up subtly different. The engine holds the back buffer and the depth surface
 * across the whole run and releases them at teardown.
 */
static void surf_AddRef(D3D8Object *self, CPU *C)
{
    d3d8_ret(C, (uint32_t)d3d8_object_addref(self));
}

static void surf_Release(D3D8Object *self, CPU *C)
{
    d3d8_ret(C, (uint32_t)d3d8_object_release(self));
}

static void surf_GetDevice(D3D8Object *self, CPU *C)
{
    uint32_t out = d3d8_arg(C, 0);
    (void)self;
    /* Same reasoning as IDirect3DDevice8::GetDirect3D: handing the device back
       needs an AddRef the caller will balance with a Release, and getting that
       pairing wrong makes teardown fail somewhere unrelated. */
    fprintf(stderr, "d3d8: IDirect3DSurface8::GetDevice is not implemented -- "
                    "handing the device back needs a reference this surface "
                    "cannot yet balance.\n");
    if (out) WR32(out, 0);
    d3d8_ret(C, D3DERR_INVALIDCALL);
}

static void surf_GetDesc(D3D8Object *self, CPU *C)
{
    D3D8Surface *s = d3d8_surface_of(self);
    uint32_t *d = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "surface description");
    if (!d) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d[0] = s->format;
    d[1] = D3DRTYPE_SURFACE;
    d[2] = s->usage;
    d[3] = s->pool;
    d[4] = s->pitch * s->height;             /* Size */
    d[5] = 0;                                /* D3DMULTISAMPLE_NONE */
    d[6] = s->width;
    d[7] = s->height;
    d3d8_ret(C, D3D_OK);
}

static void surf_LockRect(D3D8Object *self, CPU *C)
{
    D3D8Surface *s = d3d8_surface_of(self);
    uint32_t *lr = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "locked rect");
    uint32_t rect = d3d8_arg(C, 1);

    if (!lr) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (s->kind != D3D8_SURF_SYSTEM) {
        /*
         * REFUSED, and this is the honest answer rather than a gap: the back
         * buffer and the depth surface live on the GPU, and handing back a
         * host buffer would let the engine write pixels that are never seen,
         * or read pixels that were never rendered. Real D3D8 refuses an
         * unlockable surface the same way, so the engine has a path for it.
         */
        fprintf(stderr, "d3d8: LockRect on the %s. This backend does not read "
                        "back from the GPU, so there is no buffer to hand over "
                        "-- refusing, as real D3D8 does for an unlockable "
                        "surface.\n", KIND[s->kind]);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    if (rect) {
        /* A sub-rectangle lock needs the offset arithmetic and the engine has
           not asked for one; doing it silently wrong would corrupt whatever
           it wrote. */
        const uint32_t *r = (const uint32_t *)(uintptr_t)rect;
        fprintf(stderr, "d3d8: LockRect asked for the sub-rectangle "
                        "(%u,%u)-(%u,%u); this host locks whole surfaces "
                        "only.\n", r[0], r[1], r[2], r[3]);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    lr[0] = s->pitch;
    lr[1] = s->guest_pixels;
    s->locked = 1;
    d3d8_ret(C, D3D_OK);
}

static void surf_UnlockRect(D3D8Object *self, CPU *C)
{
    D3D8Surface *s = d3d8_surface_of(self);
    if (!s->locked)
        fprintf(stderr, "d3d8: UnlockRect on a surface that was not locked\n");
    s->locked = 0;
    d3d8_ret(C, D3D_OK);
}

static const D3D8MethodFn g_impl[] = {
    surf_QueryInterface,
    surf_AddRef,
    surf_Release,
    surf_GetDevice,
    NULL,                       /* 4 SetPrivateData */
    NULL,                       /* 5 GetPrivateData */
    NULL,                       /* 6 FreePrivateData */
    NULL,                       /* 7 GetContainer */
    surf_GetDesc,
    surf_LockRect,
    surf_UnlockRect
};

void d3d8_surface_install(void)
{
    d3d8_iface_implement(D3D8_IF_IDirect3DSurface8, g_impl,
                         (int)(sizeof g_impl / sizeof g_impl[0]));
}

void d3d8_surface_report(void)
{
    int i, any = 0;
    for (i = 0; i < 4; i++) any += g_count[i];
    if (!any) {
        printf("  d3d8: no surface was ever created.\n");
        return;
    }
    printf("  d3d8 surfaces:");
    for (i = 0; i < 4; i++)
        if (g_count[i]) printf("  %d %s", g_count[i], KIND[i]);
    printf("\n");
}
