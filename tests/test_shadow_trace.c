#include "shadow_trace.h"

#include <stdio.h>
#include <string.h>

static int file_contains(const char *path, const char *needle)
{
    char line[4096];
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    while (fgets(line, sizeof line, file)) {
        if (strstr(line, needle)) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

static int ordinary_answer(void)
{
    const char *path = "scratch/test-shadow-trace-off.jsonl";
    ShadowProbeCounts zero = {0, 0, 0};
    ShadowTrace trace;
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    shadow_trace_init(&trace, file, 0, 64);
    shadow_trace_control(&trace, 1, 0, 0);
    shadow_trace_default_targets(&trace, 0x1111, 0, 0x2222, 0);
    shadow_trace_present(&trace, 0, zero, 1);
    shadow_trace_draw(&trace, 0, 4, 2);
    shadow_trace_present(&trace, 0, zero, 0);
    shadow_trace_close(&trace, zero);
    fclose(file);
    return file_contains(path, "\"detailed_shadow\":0,\"path\":\"none\"")
        && file_contains(path, "\"draws\":1")
        && file_contains(path, "\"color_write_mask\":15")
        && file_contains(path, "\"stencil_mask\":4294967295")
        && file_contains(path, "\"color_write_source\":\"default\"")
        && file_contains(path, "\"render_target_source\":\"default\"")
        && file_contains(path, "\"kind\":\"default_render_target\"")
        && file_contains(path,
                         "\"original\":1,\"forced\":0,"
                         "\"observed\":0,\"expected\":0");
}

static int single_shadow_answer(const char *path, ShadowProbeCounts calls,
                                const char *answer)
{
    ShadowProbeCounts zero = {0, 0, 0};
    ShadowTrace trace;
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    shadow_trace_init(&trace, file, 1, 16);
    shadow_trace_present(&trace, 1, zero, 1);
    shadow_trace_present(&trace, 1, calls, 0);
    shadow_trace_close(&trace, calls);
    fclose(file);
    return file_contains(path, answer);
}

static int individual_shadow_answers(void)
{
    return single_shadow_answer("scratch/test-shadow-trace-planar.jsonl",
                                (ShadowProbeCounts){1, 0, 0},
                                "\"path\":\"planar\"")
        && single_shadow_answer("scratch/test-shadow-trace-projective.jsonl",
                                (ShadowProbeCounts){0, 1, 0},
                                "\"path\":\"projective\"")
        && single_shadow_answer("scratch/test-shadow-trace-self.jsonl",
                                (ShadowProbeCounts){0, 0, 1},
                                "\"path\":\"self\"");
}

static int shadow_answers_and_routes(void)
{
    const char *path = "scratch/test-shadow-trace-on.jsonl";
    ShadowProbeCounts begin = {5, 7, 11};
    ShadowProbeCounts end = {6, 9, 14};
    ShadowTrace trace;
    float matrix[16] = {
        1, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 0, 0.5f, 0.25f, 0, 1
    };
    int32_t clear_rect[4] = {3, 5, 101, 203};
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    shadow_trace_init(&trace, file, 1, 64);
    shadow_trace_present(&trace, 1, begin, 1);
    shadow_trace_resource(&trace, SHADOW_RESOURCE_CREATE_TEXTURE,
                          0x1000, 0, 256, 256, 1, 1, 21, 0, 0);
    shadow_trace_resource(&trace, SHADOW_RESOURCE_GET_SURFACE_LEVEL,
                          0x1000, 0x2000, 0, 0, 0, 0, 0, 0, 0);
    shadow_trace_set_render_target(&trace, 0x2000, 0x3000);
    shadow_trace_set_transform(&trace, 16, matrix);
    shadow_trace_set_stage_state(&trace, 0, 24, 0x104);
    shadow_trace_set_render_state(&trace, 47, 3);
    shadow_trace_set_render_state(&trace, 52, 1);
    shadow_trace_set_render_state(&trace, 168, 0);
    shadow_trace_set_pixel_shader(&trace, 3);
    shadow_trace_clear(&trace, 1, clear_rect, 7, 0x80402010u,
                       0.75f, 9, 0);
    shadow_trace_draw(&trace, 1, 4, 12);
    shadow_trace_present(&trace, 1, end, 0);
    shadow_trace_close(&trace, end);
    fclose(file);
    return file_contains(path,
                         "\"path\":\"planar+projective+self\"")
        && file_contains(path, "\"create_texture\":1")
        && file_contains(path, "\"get_surface_level\":1")
        && file_contains(path, "\"event\":\"set_transform\"")
        && file_contains(path,
                         "\"event\":\"clear\",\"frame\":1,"
                         "\"rect_count\":1,\"rects\":[[3,5,101,203]]")
        && file_contains(path, "\"zbias\":3")
        && file_contains(path, "\"color_write_mask\":0");
}

static int refusals_and_bound(void)
{
    const char *path = "scratch/test-shadow-trace-refusal.jsonl";
    ShadowProbeCounts zero = {0, 0, 0};
    ShadowTrace trace;
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    shadow_trace_init(&trace, file, 1, 1);
    shadow_trace_present(&trace, 0, zero, 1);
    shadow_trace_present(&trace, 0, zero, 0);
    shadow_trace_present(&trace, 1, zero, 1);
    shadow_trace_set_render_state(&trace, 47, 1);
    shadow_trace_set_render_state(&trace, 52, 1);
    shadow_trace_texture_hook_failure(&trace);
    shadow_trace_draw(&trace, 0, 4, 1);
    shadow_trace_present(&trace, 1, zero, 0);
    shadow_trace_close(&trace, zero);
    fclose(file);
    return file_contains(path, "\"reason\":\"detailed_shadow_mismatch\"")
        && file_contains(path, "\"events\":1,\"dropped_events\":31")
        && file_contains(path, "\"texture_hook_failures\":1");
}

static int binary_control_answers(void)
{
    int parsed = -1;
    return shadow_trace_parse_binary_control("0", &parsed) && parsed == 0
        && shadow_trace_parse_binary_control("1", &parsed) && parsed == 1
        && !shadow_trace_parse_binary_control(NULL, &parsed)
        && !shadow_trace_parse_binary_control("", &parsed)
        && !shadow_trace_parse_binary_control("2", &parsed)
        && !shadow_trace_parse_binary_control("00", &parsed)
        && !shadow_trace_parse_binary_control(" 0", &parsed)
        && !shadow_trace_parse_binary_control("0", NULL);
}

static int setting_anchor_answers(void)
{
    static const unsigned char expected[] = {
        0xA2, 0x40, 0x8D, 0xA6, 0x00
    };
    static const unsigned char wrong_build[] = {
        0xA2, 0x40, 0x8D, 0xA7, 0x00
    };
    return shadow_trace_setting_anchor_matches(expected, sizeof expected)
        && !shadow_trace_setting_anchor_matches(expected, sizeof expected - 1)
        && !shadow_trace_setting_anchor_matches(wrong_build,
                                                sizeof wrong_build)
        && !shadow_trace_setting_anchor_matches(NULL, sizeof expected);
}

int main(void)
{
    if (!ordinary_answer()) {
        fprintf(stderr, "shadow trace: ordinary-frame answer was not recorded\n");
        return 1;
    }
    if (!shadow_answers_and_routes()) {
        fprintf(stderr, "shadow trace: shadow paths/resources were not recorded\n");
        return 1;
    }
    if (!individual_shadow_answers()) {
        fprintf(stderr, "shadow trace: an individual shadow answer was lost\n");
        return 1;
    }
    if (!refusals_and_bound()) {
        fprintf(stderr, "shadow trace: mismatch refusal/event bound failed\n");
        return 1;
    }
    if (!binary_control_answers()) {
        fprintf(stderr, "shadow trace: binary control parser accepted junk\n");
        return 1;
    }
    if (!setting_anchor_answers()) {
        fprintf(stderr, "shadow trace: wrong executable anchor was accepted\n");
        return 1;
    }
    puts("shadow trace: none/planar/projective/self paths, resource route, "
         "control/build refusals and event bound proved");
    return 0;
}
