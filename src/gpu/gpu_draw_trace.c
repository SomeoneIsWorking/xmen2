#include "gpu_draw_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long g_range_index, g_range_skipped;

unsigned long gpu_draw_trace_draws_so_far(void)
{
    return g_range_index;
}

static void print_draw(FILE *dst, const GpuDraw *d, unsigned long index)
{
    fprintf(dst, "  draw %4lu %-13s x%-5u tex %-4u %-9s %s%s%s%s%s%s "
                 "stride %2u col%+3d uv%+3d cull%d zfunc%d zbias%u "
                 "stencil%d/%u,%u,%u,%u ref%u mask%x write%x rgba%x\n",
            index,
            d->prim == GPU_PRIM_TRIANGLESTRIP ? "tristrip"
            : d->prim == GPU_PRIM_LINELIST ? "linelist" : "trilist",
            d->prim_count, d->texture,
            d->texop == GPU_TEXOP_NONE ? "UNTEXTURED"
            : d->texop == GPU_TEXOP_MODULATE ? "modulate" : "select",
            d->programmable ? "VS " : "FVF ",
            d->blend_enable ? "blend " : "",
            d->depth_test ? "ztest " : "",
            d->depth_write ? "zwrite " : "",
            d->lighting ? "lit " : "unlit ",
            d->normal_offset >= 0 ? "norm" : "nonorm",
            d->vertex_stride, d->color_offset, d->uv_offset,
            d->cull, d->depth_func, d->depth_bias, d->stencil_enable,
            d->stencil_fail, d->stencil_zfail, d->stencil_pass,
            d->stencil_func, d->stencil_ref, d->stencil_mask,
            d->stencil_write_mask, d->color_write_mask);
    fprintf(dst, "           mvp [% .6g % .6g % .6g % .6g]"
                 " [% .6g % .6g % .6g % .6g]"
                 " [% .6g % .6g % .6g % .6g]"
                 " [% .6g % .6g % .6g % .6g]\n",
            d->mvp[0], d->mvp[1], d->mvp[2], d->mvp[3],
            d->mvp[4], d->mvp[5], d->mvp[6], d->mvp[7],
            d->mvp[8], d->mvp[9], d->mvp[10], d->mvp[11],
            d->mvp[12], d->mvp[13], d->mvp[14], d->mvp[15]);
}

int gpu_draw_trace_consider(const GpuDraw *d, unsigned long now)
{
    static long want = -2;
    static unsigned long busy_min, dumped, dumped_frame, rejected;
    static unsigned long seen_frame, this_draws, prev_draws;
    static int seek_vs, this_has_vs;
    static FILE *capture;
    static char *capture_text;
    static size_t capture_size;
    static long range_first = -2, range_last;
    FILE *destination;

    if (want == -2) {
        const char *value = getenv("X2_FRAME_DUMP");
        want = -1;
        if (value && *value) {
            if (!strncmp(value, "busy", 4)) {
                busy_min = value[4] == ':' ? strtoul(value + 5, NULL, 10) : 100u;
                if (!busy_min) busy_min = 100u;
                want = -3;
            } else if (!strcmp(value, "vs")) {
                seek_vs = 1;
                want = -4;
            } else {
                want = atol(value);
            }
        }
    }
    if (now != seen_frame) {
        if (capture && want == (long)seen_frame) {
            fclose(capture);
            capture = NULL;
            if (seek_vs && this_has_vs) {
                fprintf(stderr, "gpu: X2_FRAME_DUMP=vs -- frame %lu contained "
                        "a programmable draw and drew %lu times total. Every "
                        "draw of it follows.\n", seen_frame, this_draws);
                fputs(capture_text ? capture_text : "", stderr);
                seek_vs = 0;
                want = -1;
            } else if (seek_vs) {
                want = -4;
            } else if (this_draws >= busy_min) {
                fprintf(stderr, "gpu: X2_FRAME_DUMP=busy -- frame %lu drew %lu "
                        "times itself (at least %lu asked for). Every draw of "
                        "it follows.\n", seen_frame, this_draws, busy_min);
                fputs(capture_text ? capture_text : "", stderr);
                if (dumped > 400)
                    fprintf(stderr, "  ... capped at 400 draws; frame %lu had "
                            "%lu.\n", seen_frame, this_draws);
            } else {
                rejected++;
                fprintf(stderr, "gpu: X2_FRAME_DUMP=busy -- frame %lu is "
                        "DISCARDED: its predecessor drew %lu times but it drew "
                        "only %lu, under the %lu asked for. Looking for another "
                        "(%lu discarded so far).\n", seen_frame, prev_draws,
                        this_draws, busy_min, rejected);
                want = -3;
            }
            free(capture_text);
            capture_text = NULL;
            dumped = 0;
        }
        prev_draws = this_draws;
        this_draws = 0;
        this_has_vs = 0;
        seen_frame = now;
    }
    this_draws++;
    if (d->programmable) this_has_vs = 1;
    g_range_index = this_draws;
    if (want == -3 && prev_draws >= busy_min) {
        want = (long)now;
        capture = open_memstream(&capture_text, &capture_size);
        if (!capture)
            fprintf(stderr, "gpu: X2_FRAME_DUMP=busy -- cannot hold frame %ld "
                    "back (open_memstream failed); its draws go straight out "
                    "and may belong to a light frame.\n", want);
    } else if (want == -4) {
        want = (long)now;
        capture = open_memstream(&capture_text, &capture_size);
    }
    destination = capture ? capture : stderr;
    if (want >= 0 && (long)now == want) {
        if (!dumped++ && !capture)
            fprintf(stderr, "gpu: X2_FRAME_DUMP -- every draw of frame %ld "
                    "follows.\n", want);
        dumped_frame = now;
        if (dumped <= 400) print_draw(destination, d, dumped);
        else if (dumped == 401 && !capture)
            fprintf(stderr, "  ... capped at 400 draws; frame %lu had more.\n",
                    dumped_frame);
    }

    if (range_first == -2) {
        const char *value = getenv("X2_DRAW_RANGE");
        range_first = -1;
        if (value && *value) {
            const char *separator = strchr(value, ':');
            range_first = atol(value);
            range_last = separator ? atol(separator + 1) : range_first;
            if (range_last < range_first) range_last = range_first;
            fprintf(stderr, "gpu: X2_DRAW_RANGE -- ONLY draws %ld..%ld of "
                    "each frame are submitted. Every other draw is SKIPPED, "
                    "not refused. This picture is NOT a whole frame.\n",
                    range_first, range_last);
        }
    }
    if (range_first >= 0 &&
        ((long)g_range_index < range_first ||
         (long)g_range_index > range_last)) {
        g_range_skipped++;
        return 0;
    }
    return 1;
}

void gpu_draw_trace_report(void)
{
    if (g_range_skipped)
        printf("        X2_DRAW_RANGE WAS SET: %lu further draw(s) were SKIPPED "
               "(not refused). EVERY PICTURE FROM THIS RUN IS A SLICE OF A "
               "FRAME, not the frame.\n", g_range_skipped);
}
