/*
 * IDirect3DSurface8 -- a 2D image the device can render into or the guest can
 * lock.
 *
 * The engine reaches for these before it reaches for anything else it will
 * draw: igDxVisualContext's render destinations are the back buffer and the
 * depth/stencil, fetched with GetBackBuffer and GetDepthStencilSurface and
 * handed back to SetRenderTarget. That is why surfaces are the first resource
 * this host implements -- not because they are easy, but because the engine
 * stops on them.
 */
#ifndef D3D8_SURFACE_H
#define D3D8_SURFACE_H

#include <stdint.h>

#include "d3d8_com.h"

/*
 * What a surface IS, which decides what it can honestly do.
 *
 * The distinction matters for Lock: a SYSTEM surface is host memory and can
 * be locked exactly; a swapchain or depth surface belongs to the GPU and
 * locking it would mean a readback this backend does not do. The second case
 * refuses by name rather than handing back a buffer whose contents are
 * invented.
 */
typedef enum {
    D3D8_SURF_BACKBUFFER,      /* the swapchain image; owned by src/gpu */
    D3D8_SURF_DEPTHSTENCIL,    /* the device's automatic depth buffer */
    D3D8_SURF_RENDERTARGET,    /* an off-screen target the engine created */
    D3D8_SURF_SYSTEM           /* plain host memory: CreateImageSurface */
} D3D8SurfaceKind;

typedef struct {
    D3D8SurfaceKind kind;
    uint32_t        width, height;
    uint32_t        format;
    uint32_t        usage, pool;
    uint32_t        bytes_per_pixel;
    unsigned char  *pixels;        /* SYSTEM surfaces only; NULL otherwise */
    uint32_t        guest_pixels;  /* guest-addressable copy, for LockRect */
    uint32_t        pitch;
    int             locked;
} D3D8Surface;

void        d3d8_surface_install(void);
D3D8Object *d3d8_surface_new(D3D8SurfaceKind kind, uint32_t w, uint32_t h,
                             uint32_t format, uint32_t usage, uint32_t pool);
D3D8Surface *d3d8_surface_of(D3D8Object *o);

/* Bytes per pixel for a D3D8 format, or 0 if this host does not know it --
   which is refused at the call site rather than defaulted to 4. */
uint32_t d3d8_format_bpp(uint32_t format);

/* How many surfaces exist, for the shutdown report. */
void d3d8_surface_report(void);

#endif /* D3D8_SURFACE_H */
