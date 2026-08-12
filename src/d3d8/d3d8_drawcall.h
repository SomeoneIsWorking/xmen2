/*
 * The device's state, turned into a draw the GPU layer can execute.
 * See d3d8_drawcall.c for why this is separate from d3d8_device.c.
 */
#ifndef D3D8_DRAWCALL_H
#define D3D8_DRAWCALL_H

#include <stdint.h>

#include "d3d8_state.h"
#include "gpu_draw.h"

/* Byte offsets within a vertex, decoded from an FVF. -1 means absent. */
typedef struct {
    int      pos_offset;
    int      pretransformed;
    int      color_offset;
    int      uv_offset;
    int      normal_offset;
    uint32_t stride;
} D3D8VertexLayout;

/* What the draw call itself supplies, as opposed to the sticky state. */
typedef struct {
    GpuBuffer  vertex_buffer;
    uint32_t   stride;              /* 0 to use the FVF's own */
    uint32_t   first_vertex;
    GpuBuffer  index_buffer;        /* 0 for non-indexed */
    int        index_is_32bit;
    /* Guest address of the index data, so a TRIANGLEFAN can be expanded on
       the CPU. 0 when there is no index buffer. */
    uint32_t   index_guest_bytes;
    uint32_t   first_index;
    uint32_t   base_vertex;
    GpuTexture texture;             /* 0 for untextured */
    uint32_t   primitive_type;      /* D3DPRIMITIVETYPE */
    uint32_t   primitive_count;
} D3D8DrawRequest;

/* 0 if the FVF has no position this host understands. */
int d3d8_fvf_layout(uint32_t fvf, D3D8VertexLayout *out);

/* 0 and a reason if the state cannot be expressed. */
int d3d8_build_draw(const D3D8State *s, const D3D8DrawRequest *req,
                    GpuDraw *out);

void d3d8_combine_transform(const D3D8State *s, float out[16]);

void d3d8_drawcall_note_ignored_state(uint32_t which);
void d3d8_drawcall_report(void);

#endif /* D3D8_DRAWCALL_H */
