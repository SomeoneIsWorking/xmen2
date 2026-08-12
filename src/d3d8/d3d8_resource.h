/*
 * The resources the engine fills and draws from: textures, vertex buffers and
 * index buffers. See d3d8_resource.c for why each keeps a guest-addressable
 * staging copy and uploads on unlock.
 */
#ifndef D3D8_RESOURCE_H
#define D3D8_RESOURCE_H

#include <stdint.h>

#include "d3d8_com.h"
#include "gpu_draw.h"

void d3d8_resource_install(void);

D3D8Object *d3d8_texture_new(uint32_t w, uint32_t h, uint32_t levels,
                             uint32_t usage, uint32_t format, uint32_t pool);
/* A cube map: six square faces of `size`, each with its own mip chain. */
D3D8Object *d3d8_cubetexture_new(uint32_t size, uint32_t levels,
                                 uint32_t usage, uint32_t format,
                                 uint32_t pool);
D3D8Object *d3d8_vertexbuffer_new(uint32_t bytes, uint32_t usage, uint32_t fvf,
                                  uint32_t pool);
D3D8Object *d3d8_indexbuffer_new(uint32_t bytes, uint32_t usage,
                                 uint32_t format, uint32_t pool);

/* Give a resource the destructor that releases its GPU object; called by
   whoever creates it, so creation and teardown are declared together. */
void d3d8_resource_attach_destructor(D3D8Object *o);

/* What the draw path needs from a bound resource. */
GpuBuffer  d3d8_resource_buffer(D3D8Object *o);
GpuTexture d3d8_resource_texture(D3D8Object *o);
uint32_t   d3d8_resource_fvf(D3D8Object *o);
/* The buffer's length in BYTES, as the guest asked for it. */
uint32_t   d3d8_resource_bytes(D3D8Object *o);
uint32_t   d3d8_resource_guest_bytes(D3D8Object *o);
int        d3d8_resource_index_is_32bit(D3D8Object *o);

/*
 * One SUB-RESOURCE of a texture has been unlocked -- upload it.
 *
 * The sub-resource index is `face * levels + level`; for a 2D texture, where
 * there is one face, it is just the level. It is what a level surface carries
 * (a surface knows only "the level it was made for"), so a cube face's surface
 * and its texture's own UnlockRect name the same bytes the same way.
 *
 * Called both by UnlockRect and by the UnlockRect of a surface handed out by
 * GetSurfaceLevel / GetCubeMapSurface, so that the routes into the same bytes
 * cannot drift apart. Says so and uploads nothing if the index is out of range
 * or the backend refuses the copy.
 */
void d3d8_texture_level_unlocked(D3D8Object *tex, uint32_t sub);

/* How many level uploads this texture has actually completed, and the last
   level uploaded. The self-test uses them to tell "the unlock uploaded" from
   "the unlock returned OK and did nothing", which look identical otherwise. */
unsigned long d3d8_texture_uploads(D3D8Object *o);
uint32_t      d3d8_texture_last_upload_level(D3D8Object *o);

void d3d8_resource_report(void);

#endif /* D3D8_RESOURCE_H */
