#include "d3d8_selector_probe.h"

#include "guest_memory.h"
#include "gpu_device.h"

#include <string.h>

void d3d8_selector_probe_build_request(
    const D3D8State *state, const D3D8DrawRequest *request,
    unsigned long frame, D3D8SelectorProbeTicket *ticket)
{
    D3D8SelectorDrawEvidence evidence;
    D3D8VertexLayout layout;
    GpuDraw geometry;
    int fixed = state->vertex_shader <= 0xf0000000u;
    int layout_valid = fixed && d3d8_fvf_layout(state->vertex_shader, &layout);

    memset(&geometry, 0, sizeof geometry);
    geometry.vertex_stride = request->stride
        ? request->stride : (layout_valid ? layout.stride : 0u);
    geometry.pretransformed = layout_valid && layout.pretransformed;
    geometry.programmable = !fixed;
    if (layout_valid) d3d8_combine_transform(state, geometry.mvp);
    memset(&evidence, 0, sizeof evidence);
    evidence.request = request;
    evidence.draw = &geometry;
    evidence.state = state;
    if (request->vertex_guest_bytes && request->vertex_bytes
            && guest_memory_is_readable(request->vertex_guest_bytes,
                                        request->vertex_bytes))
        evidence.vertex_bytes =
            guest_memory_const_pointer(request->vertex_guest_bytes);
    if (request->index_guest_bytes && request->index_bytes
            && guest_memory_is_readable(request->index_guest_bytes,
                                        request->index_bytes))
        evidence.index_bytes =
            guest_memory_const_pointer(request->index_guest_bytes);
    evidence.element_count = d3d8_element_count(
        request->primitive_type, request->primitive_count);
    evidence.position_offset = layout_valid ? (uint32_t)layout.pos_offset : 0u;
    evidence.viewport_width = state->viewport_w > 0
        ? (uint32_t)state->viewport_w : 0u;
    evidence.viewport_height = state->viewport_h > 0
        ? (uint32_t)state->viewport_h : 0u;
    evidence.layout_valid = layout_valid;
    evidence.frame = frame;
    d3d8_selector_probe_request(&evidence, ticket);
}

int d3d8_build_draw(const D3D8State *state, const D3D8DrawRequest *request,
                    GpuDraw *draw)
{
    D3D8SelectorProbeTicket ticket;
    int accepted;

    d3d8_selector_probe_build_request(
        state, request, gpu_frames_presented(), &ticket);
    accepted = d3d8_build_draw_impl(state, request, draw);
    d3d8_selector_probe_result(&ticket, accepted);
    return accepted;
}
