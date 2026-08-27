#include "d3d8_selector_probe.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_POSITION_SAMPLES = 12,
    RS_ZENABLE = 7, RS_ZWRITEENABLE = 14, RS_ALPHATESTENABLE = 15,
    RS_SRCBLEND = 19, RS_DESTBLEND = 20, RS_CULLMODE = 22, RS_ZFUNC = 23,
    RS_ALPHAREF = 24, RS_ALPHAFUNC = 25, RS_ALPHABLENDENABLE = 27,
    RS_ZBIAS = 47,
    SS_COLOROP = 1, SS_COLORARG1 = 2, SS_COLORARG2 = 3, SS_ALPHAOP = 4,
    SS_ALPHAARG1 = 5, SS_ALPHAARG2 = 6, SS_TEXCOORDINDEX = 11,
    SS_ADDRESSU = 13, SS_ADDRESSV = 14, SS_MAGFILTER = 16,
    SS_MINFILTER = 17, SS_MIPFILTER = 18, SS_TEXTURETRANSFORMFLAGS = 24
};

typedef struct {
    int initialized;
    FILE *output;
    unsigned long frame;
    unsigned long draw_order;
    uint32_t target_width;
    uint32_t target_height;
} SelectorProbe;

static SelectorProbe g_probe;

static int read_index(const D3D8SelectorDrawEvidence *evidence, uint32_t at,
                      uint32_t *index)
{
    const D3D8DrawRequest *req = evidence->request;
    uint32_t element_size;
    uint64_t offset;
    if (!req->index_buffer) {
        if (UINT32_MAX - req->first_vertex < at) return 0;
        *index = req->first_vertex + at;
        return 1;
    }
    if (!evidence->index_bytes) return 0;
    element_size = req->index_is_32bit ? 4u : 2u;
    offset = ((uint64_t)req->first_index + at) * element_size;
    if (offset + element_size > req->index_bytes) return 0;
    if (req->index_is_32bit)
        memcpy(index, (const uint8_t *)evidence->index_bytes + offset, 4u);
    else {
        uint16_t narrow;
        memcpy(&narrow, (const uint8_t *)evidence->index_bytes + offset, 2u);
        *index = narrow;
    }
    if (UINT32_MAX - *index < req->base_vertex) return 0;
    *index += req->base_vertex;
    return 1;
}

static int read_vertex_bytes(const D3D8SelectorDrawEvidence *evidence,
                             uint32_t at, size_t count, void *bytes)
{
    uint32_t vertex;
    uint64_t offset;
    if (!evidence->vertex_bytes || !read_index(evidence, at, &vertex))
        return 0;
    offset = (uint64_t)vertex * evidence->draw->vertex_stride
           + evidence->position_offset;
    if (offset + count > evidence->request->vertex_bytes) return 0;
    memcpy(bytes, (const uint8_t *)evidence->vertex_bytes + offset, count);
    return 1;
}

static int parse_dimension(const char **text, uint32_t *value)
{
    uint32_t parsed = 0;
    const char *p = *text;
    if (*p < '0' || *p > '9') return 0;
    do {
        uint32_t digit = (uint32_t)(*p - '0');
        if (parsed > (UINT32_MAX - digit) / 10u) return 0;
        parsed = parsed * 10u + digit;
        p++;
    } while (*p >= '0' && *p <= '9');
    if (!parsed) return 0;
    *text = p;
    *value = parsed;
    return 1;
}

int d3d8_selector_texture_target_parse(const char *text,
                                       uint32_t *width, uint32_t *height)
{
    const char *cursor = text;
    uint32_t w, h;
    if (!text || !width || !height || !parse_dimension(&cursor, &w)
            || *cursor++ != 'x' || !parse_dimension(&cursor, &h)
            || *cursor)
        return 0;
    *width = w;
    *height = h;
    return 1;
}

int d3d8_selector_request_matches(
    const D3D8SelectorDrawEvidence *evidence,
    uint32_t texture_width, uint32_t texture_height)
{
    const D3D8DrawRequest *req;
    const D3D8TextureProvenance *texture;
    if (!evidence || !evidence->request) return 0;
    req = evidence->request;
    texture = &req->texture_provenance;
    return req->texture_guest != 0 && texture->metadata_valid
        && texture->width == texture_width
        && texture->height == texture_height;
}

int d3d8_selector_transformed_bounds(
    const D3D8SelectorDrawEvidence *evidence,
    D3D8SelectorBounds *bounds)
{
    const GpuDraw *draw;
    uint32_t i;
    if (!bounds) return 0;
    memset(bounds, 0, sizeof *bounds);
    if (!evidence || !evidence->request || !evidence->draw) return 0;
    draw = evidence->draw;
    bounds->requested = evidence->element_count;
    if (!evidence->element_count) return 0;
    if (!evidence->vertex_bytes || !evidence->layout_valid
            || !draw->vertex_stride || draw->programmable) {
        bounds->unavailable = bounds->requested;
        return 0;
    }
    if ((uint64_t)evidence->position_offset
            + (draw->pretransformed ? 16u : 12u) > draw->vertex_stride) {
        bounds->unavailable = bounds->requested;
        return 0;
    }
    bounds->min_x = bounds->min_y = bounds->min_z = INFINITY;
    bounds->max_x = bounds->max_y = bounds->max_z = -INFINITY;
    for (i = 0; i < evidence->element_count; ++i) {
        float p[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float x, y, z;
        if (!read_vertex_bytes(evidence, i,
                draw->pretransformed ? 16u : 12u, p)) {
            bounds->out_of_range++;
            continue;
        }
        if (draw->pretransformed) {
            x = p[0]; y = p[1]; z = p[2];
        } else {
            float clip_x = p[0] * draw->mvp[0] + p[1] * draw->mvp[4]
                         + p[2] * draw->mvp[8] + draw->mvp[12];
            float clip_y = p[0] * draw->mvp[1] + p[1] * draw->mvp[5]
                         + p[2] * draw->mvp[9] + draw->mvp[13];
            float clip_z = p[0] * draw->mvp[2] + p[1] * draw->mvp[6]
                         + p[2] * draw->mvp[10] + draw->mvp[14];
            float clip_w = p[0] * draw->mvp[3] + p[1] * draw->mvp[7]
                         + p[2] * draw->mvp[11] + draw->mvp[15];
            float viewport_x = 0.0f, viewport_y = 0.0f;
            float viewport_w = (float)evidence->viewport_width;
            float viewport_h = (float)evidence->viewport_height;
            float viewport_minz = 0.0f, viewport_maxz = 1.0f;
            if (!(clip_w > 1e-6f)) { bounds->behind++; continue; }
            if (evidence->state && evidence->state->viewport_set) {
                viewport_x = (float)evidence->state->viewport_x;
                viewport_y = (float)evidence->state->viewport_y;
                viewport_w = (float)evidence->state->viewport_w;
                viewport_h = (float)evidence->state->viewport_h;
                viewport_minz = evidence->state->viewport_minz;
                viewport_maxz = evidence->state->viewport_maxz;
            }
            if (!(viewport_w > 0.0f) || !(viewport_h > 0.0f)) {
                bounds->out_of_range++;
                continue;
            }
            x = viewport_x + (clip_x / clip_w + 1.0f) * 0.5f * viewport_w;
            y = viewport_y + (1.0f - clip_y / clip_w) * 0.5f * viewport_h;
            z = viewport_minz + (clip_z / clip_w)
                              * (viewport_maxz - viewport_minz);
        }
        if (!isfinite(x) || !isfinite(y) || !isfinite(z)) {
            bounds->out_of_range++;
            continue;
        }
        if (x < bounds->min_x) bounds->min_x = x;
        if (x > bounds->max_x) bounds->max_x = x;
        if (y < bounds->min_y) bounds->min_y = y;
        if (y > bounds->max_y) bounds->max_y = y;
        if (z < bounds->min_z) bounds->min_z = z;
        if (z > bounds->max_z) bounds->max_z = z;
        bounds->used++;
    }
    bounds->valid = bounds->used != 0;
    return bounds->valid;
}

static FILE *probe_output(void)
{
    const char *path, *target;
    if (g_probe.initialized) return g_probe.output;
    g_probe.initialized = 1;
    path = getenv("X2_SELECTOR_PROBE");
    if (!path || !*path) return NULL;
    target = getenv("X2_SELECTOR_TEXTURE");
    if (!d3d8_selector_texture_target_parse(
            target, &g_probe.target_width, &g_probe.target_height)) {
        fprintf(stderr, "selector probe: X2_SELECTOR_PROBE is armed, but "
                        "X2_SELECTOR_TEXTURE must be exact positive WxH "
                        "dimensions such as 128x32; refusing to guess a "
                        "runtime texture.\n");
        return NULL;
    }
    g_probe.output = fopen(path, "w");
    if (!g_probe.output) {
        fprintf(stderr, "selector probe: cannot open X2_SELECTOR_PROBE '%s'; "
                        "no evidence will be recorded.\n", path);
        return NULL;
    }
    setvbuf(g_probe.output, NULL, _IOLBF, 0);
    fprintf(g_probe.output,
            "{\"event\":\"meta\",\"version\":5,\"target\":"
            "\"texture-dimensions\",\"texture_width\":%u,"
            "\"texture_height\":%u,\"identity_claim\":false,"
            "\"fingerprint_algorithm\":\"fnv1a64-v1\","
            "\"fingerprint_scope\":"
            "\"2d-level0-after-successful-upload\"}\n",
            g_probe.target_width, g_probe.target_height);
    return g_probe.output;
}

static uint32_t render_state(const D3D8State *state, uint32_t which,
                             uint32_t fallback)
{
    return state && state->render[which].set
             ? state->render[which].value : fallback;
}

static uint32_t stage_state(const D3D8State *state, uint32_t which,
                            uint32_t fallback)
{
    return state && state->stage[0][which].set
             ? state->stage[0][which].value : fallback;
}

static void print_position_samples(FILE *output,
    const D3D8SelectorDrawEvidence *evidence)
{
    uint32_t count = evidence->element_count < MAX_POSITION_SAMPLES
                   ? evidence->element_count : MAX_POSITION_SAMPLES;
    uint32_t i;
    fputs(",\"position_samples\":[", output);
    for (i = 0; i < count; ++i) {
        uint32_t words[4] = {0, 0, 0, 0};
        size_t bytes = evidence->draw->pretransformed ? 16u : 12u;
        int readable = evidence->layout_valid
                    && read_vertex_bytes(evidence, i, bytes, words);
        fprintf(output, "%s", i ? "," : "");
        if (!readable) fputs("null", output);
        else if (bytes == 16u)
            fprintf(output, "[\"%08x\",\"%08x\",\"%08x\",\"%08x\"]",
                    words[0], words[1], words[2], words[3]);
        else
            fprintf(output, "[\"%08x\",\"%08x\",\"%08x\"]",
                    words[0], words[1], words[2]);
    }
    fprintf(output, "],\"positions_truncated\":%s",
            evidence->element_count > count ? "true" : "false");
}

void d3d8_selector_probe_request(const D3D8SelectorDrawEvidence *evidence,
                                 D3D8SelectorProbeTicket *ticket)
{
    D3D8SelectorBounds bounds;
    const D3D8DrawRequest *req;
    const D3D8State *state;
    const D3D8TextureProvenance *texture;
    FILE *output;
    int valid;
    if (ticket) memset(ticket, 0, sizeof *ticket);
    if (!ticket || !evidence || !evidence->request || !evidence->draw) return;
    output = probe_output();
    if (!output) return;
    if (evidence->frame != g_probe.frame) {
        g_probe.frame = evidence->frame;
        g_probe.draw_order = 0;
    }
    g_probe.draw_order++;
    if (!d3d8_selector_request_matches(
            evidence, g_probe.target_width, g_probe.target_height))
        return;
    ticket->recorded = 1;
    ticket->frame = evidence->frame;
    ticket->order = g_probe.draw_order;
    req = evidence->request;
    state = evidence->state;
    texture = &req->texture_provenance;
    valid = d3d8_selector_transformed_bounds(evidence, &bounds);
    fprintf(output,
            "{\"event\":\"candidate\",\"frame\":%lu,\"order\":%lu,"
            "\"texture_guest\":\"%08x\",\"texture_resolved\":%s,"
            "\"texture_width\":%u,\"texture_height\":%u,"
            "\"texture_format\":%u,\"texture_levels\":%u,"
            "\"texture_faces\":%u,\"fingerprint_available\":%s,"
            "\"fingerprint\":",
            ticket->frame, ticket->order, req->texture_guest,
            req->texture ? "true" : "false", texture->width, texture->height,
            texture->format, texture->levels, texture->faces,
            texture->level0_fingerprint_valid ? "true" : "false");
    if (texture->level0_fingerprint_valid)
        fprintf(output, "\"%016" PRIx64 "\"", texture->level0_fingerprint);
    else fputs("null", output);
    fprintf(output,
            ",\"fingerprint_revision\":%" PRIu64
            ",\"primitive\":%u,\"primitive_count\":%u,"
            "\"elements\":%u,\"indexed\":%s,\"fvf\":\"%08x\","
            "\"layout_valid\":%s,\"vertex_stride\":%u,"
            "\"position_offset\":%u,\"pretransformed\":%s,"
            "\"vertex_bytes\":%u,\"index_bytes\":%u,"
            "\"bounds_valid\":%s,\"min_x\":%.9g,\"min_y\":%.9g,"
            "\"min_z\":%.9g,\"max_x\":%.9g,\"max_y\":%.9g,"
            "\"max_z\":%.9g,\"elements_requested\":%u,"
            "\"elements_used\":%u,\"behind\":%u,\"out_of_range\":%u",
            texture->level0_revision, req->primitive_type,
            req->primitive_count, evidence->element_count,
            req->index_buffer ? "true" : "false",
            state ? state->vertex_shader : 0u,
            evidence->layout_valid ? "true" : "false",
            evidence->draw->vertex_stride, evidence->position_offset,
            evidence->draw->pretransformed ? "true" : "false",
            req->vertex_bytes, req->index_bytes, valid ? "true" : "false",
            valid ? bounds.min_x : 0.0f, valid ? bounds.min_y : 0.0f,
            valid ? bounds.min_z : 0.0f, valid ? bounds.max_x : 0.0f,
            valid ? bounds.max_y : 0.0f, valid ? bounds.max_z : 0.0f,
            bounds.requested, bounds.used, bounds.behind, bounds.out_of_range);
    fprintf(output, ",\"unavailable\":%u", bounds.unavailable);
    print_position_samples(output, evidence);
    fprintf(output,
            ",\"zenable\":%u,\"zwrite\":%u,\"zfunc\":%u,\"zbias\":%u,"
            "\"blend_enable\":%u,\"src_blend\":%u,\"dst_blend\":%u,"
            "\"alpha_test\":%u,\"alpha_ref\":%u,\"alpha_func\":%u,"
            "\"cull\":%u,\"color_op\":%u,\"color_arg1\":%u,"
            "\"color_arg2\":%u,\"alpha_op\":%u,\"alpha_arg1\":%u,"
            "\"alpha_arg2\":%u,\"texcoord_index\":%u,"
            "\"texture_transform\":%u,\"address_u\":%u,\"address_v\":%u,"
            "\"mag_filter\":%u,\"min_filter\":%u,\"mip_filter\":%u}\n",
            render_state(state, RS_ZENABLE, 1),
            render_state(state, RS_ZWRITEENABLE, 1),
            render_state(state, RS_ZFUNC, 4),
            render_state(state, RS_ZBIAS, 0),
            render_state(state, RS_ALPHABLENDENABLE, 0),
            render_state(state, RS_SRCBLEND, 2),
            render_state(state, RS_DESTBLEND, 1),
            render_state(state, RS_ALPHATESTENABLE, 0),
            render_state(state, RS_ALPHAREF, 0),
            render_state(state, RS_ALPHAFUNC, 8),
            render_state(state, RS_CULLMODE, 3),
            stage_state(state, SS_COLOROP, 4),
            stage_state(state, SS_COLORARG1, 2),
            stage_state(state, SS_COLORARG2, 1),
            stage_state(state, SS_ALPHAOP, 4),
            stage_state(state, SS_ALPHAARG1, 2),
            stage_state(state, SS_ALPHAARG2, 1),
            stage_state(state, SS_TEXCOORDINDEX, 0),
            stage_state(state, SS_TEXTURETRANSFORMFLAGS, 0),
            stage_state(state, SS_ADDRESSU, 1),
            stage_state(state, SS_ADDRESSV, 1),
            stage_state(state, SS_MAGFILTER, 2),
            stage_state(state, SS_MINFILTER, 2),
            stage_state(state, SS_MIPFILTER, 0));
}

void d3d8_selector_probe_result(const D3D8SelectorProbeTicket *ticket,
                                int accepted)
{
    if (!ticket || !ticket->recorded || !g_probe.output) return;
    fprintf(g_probe.output,
            "{\"event\":\"result\",\"frame\":%lu,\"order\":%lu,"
            "\"accepted\":%s}\n",
            ticket->frame, ticket->order, accepted ? "true" : "false");
}
