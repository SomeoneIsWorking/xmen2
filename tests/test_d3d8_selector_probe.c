#include "d3d8_selector_probe.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int close_enough(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static int test_texture_dimension_matcher(void)
{
    D3D8DrawRequest request;
    D3D8SelectorDrawEvidence evidence;
    memset(&request, 0, sizeof request);
    memset(&evidence, 0, sizeof evidence);
    request.texture_guest = 0x12345678u;
    d3d8_texture_provenance_init(&request.texture_provenance,
                                 128, 32, 21, 1, 1);
    evidence.request = &request;
    if (!d3d8_selector_request_matches(&evidence, 128, 32)) return 1;
    request.texture_provenance.width = 127;
    if (d3d8_selector_request_matches(&evidence, 128, 32)) return 1;
    request.texture_provenance.width = 128;
    request.texture_guest = 0;
    if (d3d8_selector_request_matches(&evidence, 128, 32)) return 1;
    request.texture_guest = 0x12345678u;
    request.texture_provenance.metadata_valid = 0;
    return d3d8_selector_request_matches(&evidence, 128, 32) ? 1 : 0;
}

static int test_texture_target_parser(void)
{
    uint32_t width = 0, height = 0;
    if (!d3d8_selector_texture_target_parse("128x32", &width, &height)
            || width != 128 || height != 32)
        return 1;
    if (d3d8_selector_texture_target_parse("128X32", &width, &height)
            || d3d8_selector_texture_target_parse("128x", &width, &height)
            || d3d8_selector_texture_target_parse("0x32", &width, &height)
            || d3d8_selector_texture_target_parse("128x32junk", &width, &height)
            || d3d8_selector_texture_target_parse("4294967296x1", &width,
                                                   &height))
        return 1;
    return 0;
}

static int test_texture_provenance_lifecycle(void)
{
    static const unsigned char hello[] = {'h', 'e', 'l', 'l', 'o'};
    static const unsigned char jello[] = {'j', 'e', 'l', 'l', 'o'};
    D3D8TextureProvenance provenance;
    uint64_t first;
    d3d8_texture_provenance_init(&provenance, 37, 19, 21, 4, 1);
    if (!provenance.metadata_valid || provenance.width != 37
            || provenance.height != 19 || provenance.format != 21
            || provenance.levels != 4 || provenance.faces != 1
            || provenance.level0_fingerprint_valid
            || provenance.level0_revision != 0)
        return 1;
    d3d8_texture_provenance_uploaded(&provenance, 0, 1,
                                     hello, sizeof hello);
    if (provenance.level0_fingerprint_valid) return 1;
    d3d8_texture_provenance_uploaded(&provenance, 0, 0,
                                     hello, sizeof hello);
    if (!provenance.level0_fingerprint_valid
            || provenance.level0_fingerprint != UINT64_C(0xa430d84680aabd0b)
            || provenance.level0_revision != 1)
        return 1;
    first = provenance.level0_fingerprint;
    d3d8_texture_provenance_uploaded(&provenance, 0, 0,
                                     hello, sizeof hello);
    if (provenance.level0_fingerprint != first
            || provenance.level0_revision != 2)
        return 1;
    d3d8_texture_provenance_uploaded(&provenance, 0, 0,
                                     jello, sizeof jello);
    if (provenance.level0_fingerprint == first
            || provenance.level0_revision != 3)
        return 1;
    d3d8_texture_provenance_init(&provenance, 32, 32, 21, 1, 6);
    d3d8_texture_provenance_uploaded(&provenance, 0, 0,
                                     hello, sizeof hello);
    return provenance.level0_fingerprint_valid ? 1 : 0;
}

static int test_pretransformed_bounds(void)
{
    const float vertices[][4] = {
        { 10.0f, 70.0f, 0.25f, 1.0f },
        { 50.0f, 20.0f, 0.75f, 1.0f },
        { 30.0f, 40.0f, 0.50f, 1.0f }
    };
    D3D8DrawRequest request;
    GpuDraw draw;
    D3D8SelectorDrawEvidence evidence;
    D3D8SelectorBounds bounds;
    memset(&request, 0, sizeof request);
    memset(&draw, 0, sizeof draw);
    memset(&evidence, 0, sizeof evidence);
    request.vertex_bytes = sizeof vertices;
    request.primitive_type = D3DPT_TRIANGLESTRIP;
    request.first_vertex = 0;
    draw.vertex_stride = sizeof vertices[0];
    draw.pretransformed = 1;
    evidence.request = &request;
    evidence.draw = &draw;
    evidence.vertex_bytes = vertices;
    evidence.element_count = 3;
    evidence.layout_valid = 1;
    if (!d3d8_selector_transformed_bounds(&evidence, &bounds)) return 1;
    if (!(bounds.used == 3 && bounds.requested == 3
          && bounds.out_of_range == 0 && bounds.behind == 0
          && bounds.unavailable == 0
          && close_enough(bounds.min_x, 10.0f)
          && close_enough(bounds.max_x, 50.0f)
          && close_enough(bounds.min_y, 20.0f)
          && close_enough(bounds.max_y, 70.0f)
          && close_enough(bounds.min_z, 0.25f)
          && close_enough(bounds.max_z, 0.75f)))
        return 1;
    evidence.vertex_bytes = NULL;
    if (d3d8_selector_transformed_bounds(&evidence, &bounds)) return 1;
    return !(bounds.requested == 3 && bounds.used == 0
          && bounds.unavailable == 3);
}

static int test_indexed_transformed_bounds(void)
{
    const float vertices[][3] = {
        { -1.0f, -1.0f, 0.1f },
        {  1.0f, -1.0f, 0.2f },
        {  1.0f,  1.0f, 0.3f },
        { -1.0f,  1.0f, 0.4f }
    };
    const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
    D3D8DrawRequest request;
    GpuDraw draw;
    D3D8State state;
    D3D8SelectorDrawEvidence evidence;
    D3D8SelectorBounds bounds;
    int i;
    memset(&request, 0, sizeof request);
    memset(&draw, 0, sizeof draw);
    memset(&state, 0, sizeof state);
    memset(&evidence, 0, sizeof evidence);
    request.vertex_bytes = sizeof vertices;
    request.index_buffer = 1;
    request.index_bytes = sizeof indices;
    draw.vertex_stride = sizeof vertices[0];
    for (i = 0; i < 16; ++i) draw.mvp[i] = (i % 5) == 0 ? 1.0f : 0.0f;
    state.viewport_set = 1;
    state.viewport_w = 800;
    state.viewport_h = 600;
    state.viewport_minz = 0.25f;
    state.viewport_maxz = 0.75f;
    evidence.request = &request;
    evidence.draw = &draw;
    evidence.state = &state;
    evidence.vertex_bytes = vertices;
    evidence.index_bytes = indices;
    evidence.element_count = 6;
    evidence.layout_valid = 1;
    if (!d3d8_selector_transformed_bounds(&evidence, &bounds)) return 1;
    return !(bounds.used == 6 && bounds.requested == 6
          && bounds.out_of_range == 0 && bounds.behind == 0
          && close_enough(bounds.min_x, 0.0f)
          && close_enough(bounds.max_x, 800.0f)
          && close_enough(bounds.min_y, 0.0f)
          && close_enough(bounds.max_y, 600.0f)
          && close_enough(bounds.min_z, 0.3f)
          && close_enough(bounds.max_z, 0.45f));
}

static int exercise_runtime_writer_when_armed(void)
{
    static const unsigned char bytes[] = {'h', 'e', 'l', 'l', 'o'};
    const float vertices[][4] = {
        {10.0f, 20.0f, 0.25f, 1.0f},
        {100.0f, 20.0f, 0.25f, 1.0f},
        {10.0f, 48.0f, 0.25f, 1.0f}
    };
    D3D8DrawRequest request;
    D3D8State state;
    GpuDraw draw;
    D3D8SelectorDrawEvidence evidence;
    D3D8SelectorProbeTicket ticket;
    const char *target = getenv("X2_SELECTOR_TEXTURE");
    if (!getenv("X2_SELECTOR_PROBE") || !target
            || strcmp(target, "128x32") != 0)
        return 0;
    memset(&request, 0, sizeof request);
    memset(&state, 0, sizeof state);
    memset(&draw, 0, sizeof draw);
    memset(&evidence, 0, sizeof evidence);
    request.vertex_bytes = sizeof vertices;
    request.texture = 1;
    request.texture_guest = 0x12345678u;
    request.primitive_type = D3DPT_TRIANGLELIST;
    request.primitive_count = 1;
    d3d8_texture_provenance_init(&request.texture_provenance,
                                 128, 32, 21, 1, 1);
    d3d8_texture_provenance_uploaded(&request.texture_provenance,
                                     0, 0, bytes, sizeof bytes);
    draw.vertex_stride = sizeof vertices[0];
    draw.pretransformed = 1;
    evidence.request = &request;
    evidence.draw = &draw;
    evidence.state = &state;
    evidence.vertex_bytes = vertices;
    evidence.element_count = 3;
    evidence.layout_valid = 1;
    evidence.frame = 7;
    d3d8_selector_probe_request(&evidence, &ticket);
    if (!ticket.recorded) return 1;
    d3d8_selector_probe_result(&ticket, 1);
    evidence.frame = 8;
    d3d8_selector_probe_request(&evidence, &ticket);
    if (!ticket.recorded) return 1;
    d3d8_selector_probe_result(&ticket, 0);
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_texture_dimension_matcher();
    failures += test_texture_target_parser();
    failures += test_texture_provenance_lifecycle();
    failures += test_pretransformed_bounds();
    failures += test_indexed_transformed_bounds();
    failures += exercise_runtime_writer_when_armed();
    printf("D3D8 selector probe: %s -- dimension matcher produced both "
           "answers, provenance tracked committed bytes, and bounds consumed "
           "every element\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
