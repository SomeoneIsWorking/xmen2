#include "gpu_draw_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long g_range_index, g_range_skipped;

typedef struct FrameDumpTrace {
  long want;
  unsigned long busy_min;
  unsigned long dumped;
  unsigned long dumped_frame;
  unsigned long rejected;
  unsigned long seen_frame;
  unsigned long this_draws;
  unsigned long prev_draws;
  int seek_vs;
  int this_has_vs;
  FILE *capture;
  char *capture_text;
  size_t capture_size;
} FrameDumpTrace;

static FrameDumpTrace g_frame_dump = {.want = -2};

#if defined(__ANDROID__)
/* Bionic's API-21 libc has funopen but not glibc's open_memstream. The frame
 * trace needs a real capture buffer so it can reject non-matching frames
 * without leaking their draws to the diagnostic output. */
typedef struct DrawTraceCapture {
  char *text;
  size_t size;
} DrawTraceCapture;

static DrawTraceCapture g_capture;

static int capture_write(void *cookie, const char *data, int size) {
  DrawTraceCapture *capture = cookie;
  size_t required;
  char *grown;
  if (size <= 0)
    return size;
  if (capture->size > SIZE_MAX - (size_t)size - 1u)
    return -1;
  required = capture->size + (size_t)size + 1u;
  grown = realloc(capture->text, required);
  if (!grown)
    return -1;
  capture->text = grown;
  memcpy(capture->text + capture->size, data, (size_t)size);
  capture->size += (size_t)size;
  capture->text[capture->size] = 0;
  return size;
}
#endif

static FILE *capture_open(char **text, size_t *size) {
  *text = NULL;
  *size = 0;
#if defined(__ANDROID__)
  free(g_capture.text);
  g_capture.text = NULL;
  g_capture.size = 0;
  return funopen(&g_capture, NULL, capture_write, NULL, NULL);
#else
  return open_memstream(text, size);
#endif
}

static void capture_close(FILE *capture, char **text, size_t *size) {
  fclose(capture);
#if defined(__ANDROID__)
  *text = g_capture.text;
  *size = g_capture.size;
  g_capture.text = NULL;
  g_capture.size = 0;
#else
  (void)text;
  (void)size;
#endif
}

unsigned long gpu_draw_trace_draws_so_far(void) { return g_range_index; }

void gpu_draw_trace_arm_busy_frame(unsigned long minimum_draws) {
  g_frame_dump.want = -3;
  g_frame_dump.busy_min = minimum_draws ? minimum_draws : 100u;
}

void gpu_draw_trace_disarm_frame_dump(void) { g_frame_dump.want = -1; }

static void print_draw(FILE *dst, const GpuDraw *d, unsigned long index) {
  fprintf(dst,
          "  draw %4lu %-13s x%-5u tex %-4u %-9s args%d/%d a%d/%d "
          "tf[%.3f %.3f %.3f %.3f] %s%s%s%s%s%s "
          "stride %2u col%+3d uv%+3d cull%d blend%d/%d "
          "zfunc%d zbias%u atest%d/ref%.3f cv%d nn%d src%d/%d/%d "
          "stencil%d/%u,%u,%u,%u ref%u mask%x write%x rgba%x\n",
          index,
          d->prim == GPU_PRIM_TRIANGLESTRIP ? "tristrip"
          : d->prim == GPU_PRIM_LINELIST    ? "linelist"
                                            : "trilist",
          d->prim_count, d->texture,
          d->texop == GPU_TEXOP_NONE       ? "UNTEXTURED"
          : d->texop == GPU_TEXOP_MODULATE ? "modulate"
                                           : "select",
          d->color_arg1, d->color_arg2, d->alpha_arg1, d->alpha_arg2,
          d->texture_factor[0], d->texture_factor[1], d->texture_factor[2],
          d->texture_factor[3], d->programmable ? "VS " : "FVF ",
          d->blend_enable ? "blend " : "", d->depth_test ? "ztest " : "",
          d->depth_write ? "zwrite " : "", d->lighting ? "lit " : "unlit ",
          d->normal_offset >= 0 ? "norm" : "nonorm", d->vertex_stride,
          d->color_offset, d->uv_offset, d->cull, d->src_blend, d->dst_blend,
          d->depth_func, d->depth_bias, d->alpha_test, d->alpha_ref,
          d->color_vertex, d->normalize_normals, d->diffuse_source,
          d->ambient_source, d->emissive_source, d->stencil_enable,
          d->stencil_fail, d->stencil_zfail, d->stencil_pass, d->stencil_func,
          d->stencil_ref, d->stencil_mask, d->stencil_write_mask,
          d->color_write_mask);
  if (d->texture && d->texture_metadata_valid)
    fprintf(dst,
            "           texture0 %ux%u fmt%u levels%u faces%u "
            "uploads%u level-mask0x%llx base%s rev%llu hash%016llx\n",
            d->texture_width, d->texture_height, d->texture_format,
            d->texture_levels, d->texture_faces, d->texture_upload_count,
            (unsigned long long)d->texture_uploaded_level_mask,
            d->texture_level0_fingerprint_valid ? "-present" : "-missing",
            (unsigned long long)d->texture_level0_revision,
            (unsigned long long)d->texture_level0_fingerprint);
  if (d->lighting) {
    int i;
    fprintf(dst,
            "           lighting n%d cv%d sources%d/%d/%d ambient "
            "[%.3f %.3f %.3f] material d[%.3f %.3f %.3f] "
            "a[%.3f %.3f %.3f] e[%.3f %.3f %.3f]\n",
            d->nlights, d->color_vertex, d->diffuse_source, d->ambient_source,
            d->emissive_source, d->global_ambient[0], d->global_ambient[1],
            d->global_ambient[2], d->mat_diffuse[0], d->mat_diffuse[1],
            d->mat_diffuse[2], d->mat_ambient[0], d->mat_ambient[1],
            d->mat_ambient[2], d->mat_emissive[0], d->mat_emissive[1],
            d->mat_emissive[2]);
    for (i = 0; i < d->nlights; ++i) {
      const GpuLight *light = &d->light[i];
      fprintf(dst,
              "           light%d type%d d[%.3f %.3f %.3f] "
              "a[%.3f %.3f %.3f] pos[%.1f %.1f %.1f] range%.1f "
              "atten[%.5f %.5f %.8f]\n",
              i, light->type, light->diffuse[0], light->diffuse[1],
              light->diffuse[2], light->ambient[0], light->ambient[1],
              light->ambient[2], light->position[0], light->position[1],
              light->position[2], light->range, light->atten[0],
              light->atten[1], light->atten[2]);
    }
  }
  if (d->texture1)
    fprintf(dst,
            "           stage1 tex %-4u op%d/%d args %d,%d/%d,%d "
            "texgen%d transform%d clamp%d point%d min%d mip%d "
            "aniso%d bias%.2f "
            "matrix row0 [% .5g % .5g % .5g % .5g] "
            "row3 [% .5g % .5g % .5g % .5g]\n",
            d->texture1, d->texop1, d->alpha_op1, d->color_arg1_1,
            d->color_arg2_1, d->alpha_arg1_1, d->alpha_arg2_1, d->texgen1,
            d->texture_transform1, d->texture_clamp1, d->texture_point1,
            d->texture_min_filter1, d->texture_mip1, d->texture_max_anisotropy1,
            d->texture_lod_bias1, d->texture_matrix1[0], d->texture_matrix1[1],
            d->texture_matrix1[2], d->texture_matrix1[3],
            d->texture_matrix1[12], d->texture_matrix1[13],
            d->texture_matrix1[14], d->texture_matrix1[15]);
  if (d->texture && (d->texgen || d->texture_transform))
    fprintf(dst,
            "           stage0 texgen%d transform0x%x min%d mip%d "
            "aniso%d bias%.2f "
            "matrix row0 [% .5g % .5g % .5g % .5g] "
            "row3 [% .5g % .5g % .5g % .5g]\n",
            d->texgen, d->texture_transform, d->texture_min_filter,
            d->texture_mip, d->texture_max_anisotropy, d->texture_lod_bias,
            d->texture_matrix[0], d->texture_matrix[1], d->texture_matrix[2],
            d->texture_matrix[3], d->texture_matrix[12], d->texture_matrix[13],
            d->texture_matrix[14], d->texture_matrix[15]);
  fprintf(dst,
          "           mvp [% .6g % .6g % .6g % .6g]"
          " [% .6g % .6g % .6g % .6g]"
          " [% .6g % .6g % .6g % .6g]"
          " [% .6g % .6g % .6g % .6g]\n",
          d->mvp[0], d->mvp[1], d->mvp[2], d->mvp[3], d->mvp[4], d->mvp[5],
          d->mvp[6], d->mvp[7], d->mvp[8], d->mvp[9], d->mvp[10], d->mvp[11],
          d->mvp[12], d->mvp[13], d->mvp[14], d->mvp[15]);
}

int gpu_draw_trace_consider(const GpuDraw *d, unsigned long now) {
  static long range_first = -2, range_last;
  static int texture_filter_init;
  static uint32_t texture_filter[16];
  static unsigned texture_filter_n;
  FILE *destination;

  if (g_frame_dump.want == -2) {
    const char *value = getenv("X2_FRAME_DUMP");
    g_frame_dump.want = -1;
    if (value && *value) {
      if (!strncmp(value, "busy", 4)) {
        g_frame_dump.busy_min =
            value[4] == ':' ? strtoul(value + 5, NULL, 10) : 100u;
        if (!g_frame_dump.busy_min)
          g_frame_dump.busy_min = 100u;
        g_frame_dump.want = -3;
      } else if (!strcmp(value, "vs")) {
        g_frame_dump.seek_vs = 1;
        g_frame_dump.want = -4;
      } else {
        g_frame_dump.want = atol(value);
      }
    }
  }
  if (now != g_frame_dump.seen_frame) {
    if (g_frame_dump.capture &&
        g_frame_dump.want == (long)g_frame_dump.seen_frame) {
      capture_close(g_frame_dump.capture, &g_frame_dump.capture_text,
                    &g_frame_dump.capture_size);
      g_frame_dump.capture = NULL;
      if (g_frame_dump.seek_vs && g_frame_dump.this_has_vs) {
        fprintf(stderr,
                "gpu: X2_FRAME_DUMP=vs -- frame %lu contained "
                "a programmable draw and drew %lu times total. Every "
                "draw of it follows.\n",
                g_frame_dump.seen_frame, g_frame_dump.this_draws);
        fputs(g_frame_dump.capture_text ? g_frame_dump.capture_text : "",
              stderr);
        g_frame_dump.seek_vs = 0;
        g_frame_dump.want = -1;
      } else if (g_frame_dump.seek_vs) {
        g_frame_dump.want = -4;
      } else if (g_frame_dump.this_draws >= g_frame_dump.busy_min) {
        fprintf(stderr,
                "gpu: X2_FRAME_DUMP=busy -- frame %lu drew %lu "
                "times itself (at least %lu asked for). Every draw of "
                "it follows.\n",
                g_frame_dump.seen_frame, g_frame_dump.this_draws,
                g_frame_dump.busy_min);
        fputs(g_frame_dump.capture_text ? g_frame_dump.capture_text : "",
              stderr);
        if (g_frame_dump.dumped > 400)
          fprintf(stderr,
                  "  ... capped at 400 draws; frame %lu had "
                  "%lu.\n",
                  g_frame_dump.seen_frame, g_frame_dump.this_draws);
      } else {
        g_frame_dump.rejected++;
        fprintf(stderr,
                "gpu: X2_FRAME_DUMP=busy -- frame %lu is "
                "DISCARDED: its predecessor drew %lu times but it drew "
                "only %lu, under the %lu asked for. Looking for another "
                "(%lu discarded so far).\n",
                g_frame_dump.seen_frame, g_frame_dump.prev_draws,
                g_frame_dump.this_draws, g_frame_dump.busy_min,
                g_frame_dump.rejected);
        g_frame_dump.want = -3;
      }
      free(g_frame_dump.capture_text);
      g_frame_dump.capture_text = NULL;
      g_frame_dump.dumped = 0;
    }
    g_frame_dump.prev_draws = g_frame_dump.this_draws;
    g_frame_dump.this_draws = 0;
    g_frame_dump.this_has_vs = 0;
    g_frame_dump.seen_frame = now;
  }
  g_frame_dump.this_draws++;
  if (d->programmable)
    g_frame_dump.this_has_vs = 1;
  g_range_index = g_frame_dump.this_draws;
  if (g_frame_dump.want == -3 &&
      g_frame_dump.prev_draws >= g_frame_dump.busy_min) {
    g_frame_dump.want = (long)now;
    g_frame_dump.capture =
        capture_open(&g_frame_dump.capture_text, &g_frame_dump.capture_size);
    if (!g_frame_dump.capture)
      fprintf(stderr,
              "gpu: X2_FRAME_DUMP=busy -- cannot hold frame %ld "
              "back (memory capture failed); its draws go straight out "
              "and may belong to a light frame.\n",
              g_frame_dump.want);
  } else if (g_frame_dump.want == -4) {
    g_frame_dump.want = (long)now;
    g_frame_dump.capture =
        capture_open(&g_frame_dump.capture_text, &g_frame_dump.capture_size);
  }
  destination = g_frame_dump.capture ? g_frame_dump.capture : stderr;
  if (g_frame_dump.want >= 0 && (long)now == g_frame_dump.want) {
    if (!g_frame_dump.dumped++ && !g_frame_dump.capture)
      fprintf(stderr,
              "gpu: X2_FRAME_DUMP -- every draw of frame %ld "
              "follows.\n",
              g_frame_dump.want);
    g_frame_dump.dumped_frame = now;
    if (g_frame_dump.dumped <= 400)
      print_draw(destination, d, g_frame_dump.dumped);
    else if (g_frame_dump.dumped == 401 && !g_frame_dump.capture)
      fprintf(stderr, "  ... capped at 400 draws; frame %lu had more.\n",
              g_frame_dump.dumped_frame);
  }

  if (range_first == -2) {
    const char *value = getenv("X2_DRAW_RANGE");
    range_first = -1;
    if (value && *value) {
      const char *separator = strchr(value, ':');
      range_first = atol(value);
      range_last = separator ? atol(separator + 1) : range_first;
      if (range_last < range_first)
        range_last = range_first;
      fprintf(stderr,
              "gpu: X2_DRAW_RANGE -- ONLY draws %ld..%ld of "
              "each frame are submitted. Every other draw is SKIPPED, "
              "not refused. This picture is NOT a whole frame.\n",
              range_first, range_last);
    }
  }
  if (range_first >= 0 &&
      ((long)g_range_index < range_first || (long)g_range_index > range_last)) {
    g_range_skipped++;
    return 0;
  }
  if (!texture_filter_init) {
    const char *value = getenv("X2_DRAW_TEXTURES");
    texture_filter_init = 1;
    if (value && *value) {
      char copy[256], *part, *save;
      snprintf(copy, sizeof copy, "%s", value);
      for (part = strtok_r(copy, ",", &save);
           part &&
           texture_filter_n < sizeof texture_filter / sizeof texture_filter[0];
           part = strtok_r(NULL, ",", &save))
        texture_filter[texture_filter_n++] = (uint32_t)strtoul(part, NULL, 0);
      fprintf(stderr,
              "gpu: X2_DRAW_TEXTURES -- ONLY draws using one "
              "of %u named texture handle(s) are submitted. "
              "This picture is NOT a whole frame.\n",
              texture_filter_n);
    }
  }
  if (texture_filter_n) {
    unsigned i;
    int matched = 0;
    for (i = 0; i < texture_filter_n; i++)
      if (d->texture == texture_filter[i] || d->texture1 == texture_filter[i]) {
        matched = 1;
        break;
      }
    if (!matched) {
      g_range_skipped++;
      return 0;
    }
  }
  return 1;
}

void gpu_draw_trace_report(void) {
  if (g_range_skipped)
    printf("        X2_DRAW_RANGE WAS SET: %lu further draw(s) were SKIPPED "
           "(not refused). EVERY PICTURE FROM THIS RUN IS A SLICE OF A "
           "FRAME, not the frame.\n",
           g_range_skipped);
}
