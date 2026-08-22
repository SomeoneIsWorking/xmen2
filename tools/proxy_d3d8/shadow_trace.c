#include "shadow_trace.h"

#include <math.h>
#include <string.h>

enum {
    D3DRS_ZENABLE = 7,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_ZBIAS = 47,
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_COLORWRITEENABLE = 168
};

enum {
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DCULL_CCW = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_ALWAYS = 8,
    D3DSTENCILOP_KEEP = 1,
    D3DCOLORWRITEENABLE_ALL = 0x0f
};

static const uint32_t SNAPSHOT_STATES[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_CULLMODE, D3DRS_ZFUNC,
    D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE,
    D3DRS_ZBIAS, D3DRS_STENCILENABLE, D3DRS_STENCILFAIL,
    D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC,
    D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
    D3DRS_COLORWRITEENABLE
};

static const char *resource_name(ShadowResourceKind kind)
{
    static const char *const NAME[] = {
        "create_texture", "get_surface_level", "create_render_target",
        "create_depth_stencil", "copy_rects", "update_texture",
        "default_render_target", "default_depth_stencil"
    };
    return kind < SHADOW_RESOURCE_COUNT ? NAME[kind] : "invalid";
}

static void flush_line(ShadowTrace *trace)
{
    fputc('\n', trace->out);
    fflush(trace->out);
}

static int event_slot(ShadowTrace *trace)
{
    if (!trace->capturing) return 0;
    if (trace->events >= trace->max_events) {
        trace->dropped_events++;
        return 0;
    }
    trace->events++;
    return 1;
}

static unsigned probe_delta(unsigned end, unsigned begin)
{
    return end >= begin ? end - begin : end;
}

static void write_float_json(FILE *out, float value)
{
    if (isnan(value)) fputs("\"nan\"", out);
    else if (isinf(value))
        fputs(signbit(value) ? "\"-inf\"" : "\"+inf\"", out);
    else fprintf(out, "%.9g", (double)value);
}

static void write_matrix(ShadowTrace *trace, const char *event,
                         uint32_t which, const float matrix[16])
{
    unsigned i;
    fprintf(trace->out,
            "{\"event\":\"%s\",\"frame\":%u,\"which\":%lu,\"m\":[",
            event, trace->frame, (unsigned long)which);
    for (i = 0; i < 16; i++) {
        if (i) fputc(',', trace->out);
        write_float_json(trace->out, matrix[i]);
    }
    fputs("]}", trace->out);
    flush_line(trace);
}

static void write_snapshot(ShadowTrace *trace)
{
    unsigned i, stage, which;

    if (event_slot(trace))
        fprintf(trace->out,
                "{\"event\":\"snapshot\",\"frame\":%u,"
                "\"render_target\":\"0x%llx\","
                "\"depth_stencil\":\"0x%llx\","
                "\"render_target_source\":\"%s\","
                "\"depth_stencil_source\":\"%s\","
                "\"pixel_shader\":%lu}\n",
                trace->frame, (unsigned long long)trace->render_target,
                (unsigned long long)trace->depth_stencil,
                !trace->render_target_known ? "unknown"
                    : trace->render_target_is_default ? "default" : "explicit",
                !trace->depth_stencil_known ? "unknown"
                    : trace->depth_stencil_is_default ? "default" : "explicit",
                (unsigned long)trace->pixel_shader);
    for (i = 0; i < sizeof SNAPSHOT_STATES / sizeof SNAPSHOT_STATES[0]; i++) {
        which = SNAPSHOT_STATES[i];
        if (event_slot(trace))
            fprintf(trace->out,
                    "{\"event\":\"snapshot_render_state\",\"frame\":%u,"
                    "\"which\":%u,\"source\":\"%s\","
                    "\"value\":%lu}\n",
                    trace->frame, which,
                    trace->render_state_set[which] ? "explicit" : "default",
                    (unsigned long)trace->render_state[which]);
    }
    for (stage = 0; stage < SHADOW_TRACE_STAGES; stage++) {
        if (event_slot(trace))
            fprintf(trace->out,
                    "{\"event\":\"snapshot_texture\",\"frame\":%u,"
                    "\"stage\":%u,\"texture\":\"0x%llx\"}\n",
                    trace->frame, stage,
                    (unsigned long long)trace->texture[stage]);
        for (which = 0; which < SHADOW_TRACE_STAGE_STATES; which++)
            if (trace->stage_state_set[stage][which] && event_slot(trace))
                fprintf(trace->out,
                        "{\"event\":\"snapshot_stage_state\","
                        "\"frame\":%u,\"stage\":%u,\"which\":%u,"
                        "\"value\":%lu}\n",
                        trace->frame, stage, which,
                        (unsigned long)trace->stage_state[stage][which]);
    }
    for (which = 0; which < SHADOW_TRACE_TRANSFORMS; which++)
        if (trace->transform[which].set && event_slot(trace))
            write_matrix(trace, "snapshot_transform", which,
                         trace->transform[which].m);
    fflush(trace->out);
}

static void begin_capture(ShadowTrace *trace, int detailed,
                          ShadowProbeCounts probes)
{
    memset(trace->frame_resources, 0, sizeof trace->frame_resources);
    trace->capturing = 1;
    trace->capture_detailed = detailed;
    trace->events = 0;
    trace->dropped_events = 0;
    trace->draws = 0;
    trace->indexed_draws = 0;
    trace->probe_begin = probes;
    fprintf(trace->out,
            "{\"event\":\"begin\",\"frame\":%u,"
            "\"detailed_shadow\":%d,\"expected\":%d,"
            "\"max_events\":%u}\n",
            trace->frame, detailed, trace->expected_detailed,
            trace->max_events);
    write_snapshot(trace);
}

static void end_capture(ShadowTrace *trace, ShadowProbeCounts probes)
{
    unsigned planar = probe_delta(probes.planar, trace->probe_begin.planar);
    unsigned projective = probe_delta(probes.projective,
                                      trace->probe_begin.projective);
    unsigned self_shadow = probe_delta(probes.self_shadow,
                                       trace->probe_begin.self_shadow);
    char path[48];

    path[0] = 0;
    if (planar) strcat(path, "planar");
    if (projective) strcat(path, path[0] ? "+projective" : "projective");
    if (self_shadow) strcat(path, path[0] ? "+self" : "self");
    if (!path[0]) strcpy(path, "none");

    fprintf(trace->out,
            "{\"event\":\"summary\",\"frame\":%u,"
            "\"detailed_shadow\":%d,\"path\":\"%s\","
            "\"shade_calls\":{\"planar\":%u,\"projective\":%u,"
            "\"self\":%u},\"draws\":%u,\"indexed_draws\":%u,"
            "\"events\":%u,\"dropped_events\":%u,"
            "\"resource_events\":%u,\"dropped_resource_events\":%u,"
            "\"texture_hook_failures\":%u,"
            "\"frame_resources\":{\"create_texture\":%u,"
            "\"get_surface_level\":%u,\"create_render_target\":%u,"
            "\"create_depth_stencil\":%u,\"copy_rects\":%u,"
            "\"update_texture\":%u,\"default_render_target\":%u,"
            "\"default_depth_stencil\":%u},"
            "\"total_resources\":{\"create_texture\":%u,"
            "\"get_surface_level\":%u,\"create_render_target\":%u,"
            "\"create_depth_stencil\":%u,\"copy_rects\":%u,"
            "\"update_texture\":%u,\"default_render_target\":%u,"
            "\"default_depth_stencil\":%u}}\n",
            trace->frame, trace->capture_detailed, path,
            planar, projective, self_shadow,
            trace->draws, trace->indexed_draws, trace->events,
            trace->dropped_events, trace->resource_events,
            trace->dropped_resource_events, trace->texture_hook_failures,
            trace->frame_resources[SHADOW_RESOURCE_CREATE_TEXTURE],
            trace->frame_resources[SHADOW_RESOURCE_GET_SURFACE_LEVEL],
            trace->frame_resources[SHADOW_RESOURCE_CREATE_RENDER_TARGET],
            trace->frame_resources[SHADOW_RESOURCE_CREATE_DEPTH_STENCIL],
            trace->frame_resources[SHADOW_RESOURCE_COPY_RECTS],
            trace->frame_resources[SHADOW_RESOURCE_UPDATE_TEXTURE],
            trace->frame_resources[SHADOW_RESOURCE_DEFAULT_RENDER_TARGET],
            trace->frame_resources[SHADOW_RESOURCE_DEFAULT_DEPTH_STENCIL],
            trace->total_resources[SHADOW_RESOURCE_CREATE_TEXTURE],
            trace->total_resources[SHADOW_RESOURCE_GET_SURFACE_LEVEL],
            trace->total_resources[SHADOW_RESOURCE_CREATE_RENDER_TARGET],
            trace->total_resources[SHADOW_RESOURCE_CREATE_DEPTH_STENCIL],
            trace->total_resources[SHADOW_RESOURCE_COPY_RECTS],
            trace->total_resources[SHADOW_RESOURCE_UPDATE_TEXTURE],
            trace->total_resources[SHADOW_RESOURCE_DEFAULT_RENDER_TARGET],
            trace->total_resources[SHADOW_RESOURCE_DEFAULT_DEPTH_STENCIL]);
    fflush(trace->out);
    trace->capturing = 0;
}

void shadow_trace_texture_hook_failure(ShadowTrace *trace)
{
    if (!trace->enabled) return;
    trace->texture_hook_failures++;
}

void shadow_trace_default_targets(ShadowTrace *trace, uintptr_t target,
                                  long target_result,
                                  uintptr_t depth_stencil,
                                  long depth_result)
{
    if (!trace->enabled) return;
    trace->render_target = target;
    trace->depth_stencil = depth_stencil;
    trace->render_target_known = target_result >= 0;
    trace->depth_stencil_known = depth_result >= 0;
    trace->render_target_is_default = trace->render_target_known;
    trace->depth_stencil_is_default = trace->depth_stencil_known;
    shadow_trace_resource(trace, SHADOW_RESOURCE_DEFAULT_RENDER_TARGET,
                          target, 0, 0, 0, 0, 0, 0, 0, target_result);
    shadow_trace_resource(trace, SHADOW_RESOURCE_DEFAULT_DEPTH_STENCIL,
                          depth_stencil, 0, 0, 0, 0, 0, 0, 0, depth_result);
}

void shadow_trace_init(ShadowTrace *trace, FILE *out, int expected_detailed,
                       unsigned max_events)
{
    memset(trace, 0, sizeof *trace);
    trace->out = out;
    trace->expected_detailed = expected_detailed;
    trace->max_events = max_events ? max_events : 1u;
    trace->enabled = out != NULL && (expected_detailed == 0 || expected_detailed == 1);
    trace->render_state[D3DRS_ZENABLE] = 1;
    trace->render_state[D3DRS_ZWRITEENABLE] = 1;
    trace->render_state[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    trace->render_state[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    trace->render_state[D3DRS_CULLMODE] = D3DCULL_CCW;
    trace->render_state[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    trace->render_state[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    trace->render_state[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    trace->render_state[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    trace->render_state[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    trace->render_state[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    trace->render_state[D3DRS_STENCILMASK] = UINT32_MAX;
    trace->render_state[D3DRS_STENCILWRITEMASK] = UINT32_MAX;
    trace->render_state[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
    if (trace->enabled) {
        fprintf(trace->out,
                "{\"event\":\"meta\",\"format\":1,\"expected\":%d,"
                "\"max_events\":%u}\n",
                expected_detailed, trace->max_events);
        fflush(trace->out);
    }
}

int shadow_trace_parse_binary_control(const char *value, int *parsed)
{
    if (!value || !parsed || (value[0] != '0' && value[0] != '1')
            || value[1] != '\0')
        return 0;
    *parsed = value[0] - '0';
    return 1;
}

int shadow_trace_setting_anchor_matches(const unsigned char *instruction,
                                        size_t instruction_size)
{
    static const unsigned char expected[] = {
        0xA2, 0x40, 0x8D, 0xA6, 0x00
    };
    return instruction && instruction_size >= sizeof expected
        && memcmp(instruction, expected, sizeof expected) == 0;
}

void shadow_trace_control(ShadowTrace *trace, int original_detailed,
                          int forced_detailed, int observed_detailed)
{
    if (!trace->enabled) return;
    fprintf(trace->out,
            "{\"event\":\"control\","
            "\"seam\":\"XMen2.exe+0x668d40\","
            "\"original\":%d,\"forced\":%d,\"observed\":%d,"
            "\"expected\":%d}\n",
            original_detailed, forced_detailed, observed_detailed,
            trace->expected_detailed);
    fflush(trace->out);
}

void shadow_trace_present(ShadowTrace *trace, int detailed_shadow,
                          ShadowProbeCounts probes, int arm_key_down)
{
    int arm_edge;
    if (!trace->enabled) return;
    if (trace->capturing) end_capture(trace, probes);
    trace->frame++;
    arm_edge = arm_key_down && !trace->arm_down;
    trace->arm_down = arm_key_down;
    if (!arm_edge) return;
    if (detailed_shadow != trace->expected_detailed) {
        fprintf(trace->out,
                "{\"event\":\"refusal\",\"frame\":%u,"
                "\"reason\":\"detailed_shadow_mismatch\","
                "\"actual\":%d,\"expected\":%d}\n",
                trace->frame, detailed_shadow, trace->expected_detailed);
        fflush(trace->out);
        return;
    }
    begin_capture(trace, detailed_shadow, probes);
}

void shadow_trace_close(ShadowTrace *trace, ShadowProbeCounts probes)
{
    if (!trace->enabled) return;
    if (trace->capturing)
        end_capture(trace, probes);
    fprintf(trace->out,
            "{\"event\":\"close\",\"frames_seen\":%u,"
            "\"capturing\":0}\n",
            trace->frame);
    fflush(trace->out);
}

void shadow_trace_resource(ShadowTrace *trace, ShadowResourceKind kind,
                           uintptr_t object, uintptr_t related,
                           uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t e, uint32_t f, long result)
{
    if (!trace->enabled || kind >= SHADOW_RESOURCE_COUNT) return;
    trace->total_resources[kind]++;
    if (trace->capturing) trace->frame_resources[kind]++;
    if (trace->capturing) {
        if (!event_slot(trace)) return;
    } else {
        if (trace->resource_events >= trace->max_events) {
            trace->dropped_resource_events++;
            return;
        }
    }
    trace->resource_events++;
    fprintf(trace->out,
            "{\"event\":\"resource\",\"frame\":%u,\"kind\":\"%s\","
            "\"object\":\"0x%llx\",\"related\":\"0x%llx\"",
            trace->frame, resource_name(kind), (unsigned long long)object,
            (unsigned long long)related);
    switch (kind) {
    case SHADOW_RESOURCE_CREATE_TEXTURE:
        fprintf(trace->out,
                ",\"width\":%lu,\"height\":%lu,\"levels\":%lu,"
                "\"usage\":%lu,\"format\":%lu,\"pool\":%lu",
                (unsigned long)a, (unsigned long)b, (unsigned long)c,
                (unsigned long)d, (unsigned long)e, (unsigned long)f);
        break;
    case SHADOW_RESOURCE_GET_SURFACE_LEVEL:
        fprintf(trace->out, ",\"level\":%lu", (unsigned long)a);
        break;
    case SHADOW_RESOURCE_CREATE_RENDER_TARGET:
        fprintf(trace->out,
                ",\"width\":%lu,\"height\":%lu,\"format\":%lu,"
                "\"multisample\":%lu,\"lockable\":%lu",
                (unsigned long)a, (unsigned long)b, (unsigned long)c,
                (unsigned long)d, (unsigned long)e);
        break;
    case SHADOW_RESOURCE_CREATE_DEPTH_STENCIL:
        fprintf(trace->out,
                ",\"width\":%lu,\"height\":%lu,\"format\":%lu,"
                "\"multisample\":%lu",
                (unsigned long)a, (unsigned long)b, (unsigned long)c,
                (unsigned long)d);
        break;
    case SHADOW_RESOURCE_COPY_RECTS:
        fprintf(trace->out,
                ",\"rect_count\":%lu,\"has_source_rects\":%lu,"
                "\"has_destination_points\":%lu",
                (unsigned long)a, (unsigned long)b, (unsigned long)c);
        break;
    case SHADOW_RESOURCE_UPDATE_TEXTURE:
    case SHADOW_RESOURCE_DEFAULT_RENDER_TARGET:
    case SHADOW_RESOURCE_DEFAULT_DEPTH_STENCIL:
    case SHADOW_RESOURCE_COUNT:
        break;
    }
    fprintf(trace->out, ",\"result\":%ld}", result);
    flush_line(trace);
}

void shadow_trace_set_render_target(ShadowTrace *trace, uintptr_t target,
                                    uintptr_t depth_stencil)
{
    trace->render_target = target;
    trace->depth_stencil = depth_stencil;
    trace->render_target_known = 1;
    trace->depth_stencil_known = 1;
    trace->render_target_is_default = 0;
    trace->depth_stencil_is_default = 0;
    if (!trace->enabled || !event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"set_render_target\",\"frame\":%u,"
            "\"target\":\"0x%llx\",\"depth_stencil\":\"0x%llx\"}",
            trace->frame, (unsigned long long)target,
            (unsigned long long)depth_stencil);
    flush_line(trace);
}

void shadow_trace_set_transform(ShadowTrace *trace, uint32_t which,
                                const float matrix[16])
{
    if (which >= SHADOW_TRACE_TRANSFORMS || !matrix) return;
    trace->transform[which].set = 1;
    memcpy(trace->transform[which].m, matrix, sizeof trace->transform[which].m);
    if (!trace->enabled || !event_slot(trace)) return;
    write_matrix(trace, "set_transform", which, matrix);
}

void shadow_trace_set_render_state(ShadowTrace *trace, uint32_t which,
                                   uint32_t value)
{
    if (which >= SHADOW_TRACE_RENDER_STATES) return;
    trace->render_state_set[which] = 1;
    trace->render_state[which] = value;
    if (!trace->enabled || !event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"set_render_state\",\"frame\":%u,"
            "\"which\":%lu,\"value\":%lu}",
            trace->frame, (unsigned long)which, (unsigned long)value);
    flush_line(trace);
}

void shadow_trace_set_texture(ShadowTrace *trace, uint32_t stage,
                              uintptr_t texture)
{
    if (stage >= SHADOW_TRACE_STAGES) return;
    trace->texture[stage] = texture;
    if (!trace->enabled || !event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"set_texture\",\"frame\":%u,"
            "\"stage\":%lu,\"texture\":\"0x%llx\"}",
            trace->frame, (unsigned long)stage, (unsigned long long)texture);
    flush_line(trace);
}

void shadow_trace_set_stage_state(ShadowTrace *trace, uint32_t stage,
                                  uint32_t which, uint32_t value)
{
    if (stage >= SHADOW_TRACE_STAGES || which >= SHADOW_TRACE_STAGE_STATES) return;
    trace->stage_state_set[stage][which] = 1;
    trace->stage_state[stage][which] = value;
    if (!trace->enabled || !event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"set_stage_state\",\"frame\":%u,"
            "\"stage\":%lu,\"which\":%lu,\"value\":%lu}",
            trace->frame, (unsigned long)stage, (unsigned long)which,
            (unsigned long)value);
    flush_line(trace);
}

void shadow_trace_set_pixel_shader(ShadowTrace *trace, uint32_t handle)
{
    trace->pixel_shader = handle;
    if (!trace->enabled || !event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"set_pixel_shader\",\"frame\":%u,"
            "\"handle\":%lu}", trace->frame, (unsigned long)handle);
    flush_line(trace);
}

void shadow_trace_draw(ShadowTrace *trace, int indexed, uint32_t primitive,
                       uint32_t primitive_count)
{
    if (!trace->capturing) return;
    trace->draws++;
    if (indexed) trace->indexed_draws++;
    if (!event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"draw\",\"frame\":%u,\"ordinal\":%u,"
            "\"indexed\":%d,\"primitive\":%lu,\"primitive_count\":%lu,"
            "\"render_target\":\"0x%llx\",\"depth_stencil\":\"0x%llx\","
            "\"pixel_shader\":%lu,\"texture0\":\"0x%llx\","
            "\"zbias\":%lu,\"stencil_enable\":%lu,"
            "\"stencil_fail\":%lu,\"stencil_zfail\":%lu,"
            "\"stencil_pass\":%lu,\"stencil_func\":%lu,"
            "\"stencil_ref\":%lu,\"stencil_mask\":%lu,"
            "\"stencil_write_mask\":%lu,\"color_write_mask\":%lu,"
            "\"zbias_source\":\"%s\","
            "\"stencil_source\":\"%s\","
            "\"color_write_source\":\"%s\","
            "\"render_target_source\":\"%s\","
            "\"depth_stencil_source\":\"%s\"}",
            trace->frame, trace->draws, indexed ? 1 : 0,
            (unsigned long)primitive, (unsigned long)primitive_count,
            (unsigned long long)trace->render_target,
            (unsigned long long)trace->depth_stencil,
            (unsigned long)trace->pixel_shader,
            (unsigned long long)trace->texture[0],
            (unsigned long)trace->render_state[D3DRS_ZBIAS],
            (unsigned long)trace->render_state[D3DRS_STENCILENABLE],
            (unsigned long)trace->render_state[D3DRS_STENCILFAIL],
            (unsigned long)trace->render_state[D3DRS_STENCILZFAIL],
            (unsigned long)trace->render_state[D3DRS_STENCILPASS],
            (unsigned long)trace->render_state[D3DRS_STENCILFUNC],
            (unsigned long)trace->render_state[D3DRS_STENCILREF],
            (unsigned long)trace->render_state[D3DRS_STENCILMASK],
            (unsigned long)trace->render_state[D3DRS_STENCILWRITEMASK],
            (unsigned long)trace->render_state[D3DRS_COLORWRITEENABLE],
            trace->render_state_set[D3DRS_ZBIAS] ? "explicit" : "default",
            trace->render_state_set[D3DRS_STENCILENABLE]
                ? "explicit" : "default",
            trace->render_state_set[D3DRS_COLORWRITEENABLE]
                ? "explicit" : "default",
            !trace->render_target_known ? "unknown"
                : trace->render_target_is_default ? "default" : "explicit",
            !trace->depth_stencil_known ? "unknown"
                : trace->depth_stencil_is_default ? "default" : "explicit");
    flush_line(trace);
}

void shadow_trace_clear(ShadowTrace *trace, uint32_t rect_count,
                        const int32_t *rects, uint32_t flags, uint32_t color,
                        float depth, uint32_t stencil, long result)
{
    uint32_t i;
    if (!trace->capturing) return;
    if (rect_count > 64u) {
        trace->dropped_events++;
        return;
    }
    if (!event_slot(trace)) return;
    fprintf(trace->out,
            "{\"event\":\"clear\",\"frame\":%u,\"rect_count\":%lu,"
            "\"rects\":",
            trace->frame, (unsigned long)rect_count);
    if (!rects) {
        fputs("null", trace->out);
    } else {
        fputc('[', trace->out);
        for (i = 0; i < rect_count; i++) {
            if (i) fputc(',', trace->out);
            fprintf(trace->out, "[%ld,%ld,%ld,%ld]",
                    (long)rects[i * 4u], (long)rects[i * 4u + 1u],
                    (long)rects[i * 4u + 2u], (long)rects[i * 4u + 3u]);
        }
        fputc(']', trace->out);
    }
    fprintf(trace->out,
            ",\"flags\":%lu,\"color\":%lu,\"depth\":",
            (unsigned long)flags, (unsigned long)color);
    write_float_json(trace->out, depth);
    fprintf(trace->out,
            ",\"stencil\":%lu,\"render_target\":\"0x%llx\","
            "\"depth_stencil\":\"0x%llx\",\"result\":%ld}",
            (unsigned long)stencil,
            (unsigned long long)trace->render_target,
            (unsigned long long)trace->depth_stencil, result);
    flush_line(trace);
}
