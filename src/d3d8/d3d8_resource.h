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
int        d3d8_resource_index_is_32bit(D3D8Object *o);

void d3d8_resource_report(void);

#endif /* D3D8_RESOURCE_H */
