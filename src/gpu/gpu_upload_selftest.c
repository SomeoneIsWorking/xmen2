/* The upload-staging lifetime contract, exercised on the real SDL GPU path. */
#include "../native/x2_log.h"
#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_internal.h"
#include "gpu_selftests.h"
#include "gpu_upload_batch.h"

#include <stdio.h>
#include <string.h>

int gpu_upload_reuse_selftest(void) {
  enum { kBuffers = 8, kUploadsEach = 8 };
  unsigned char data[64];
  unsigned long long draw_ns, upload_ns, alloc_ns, submit_ns;
  unsigned long long allocs_before, allocs_after, allocs_second_frame;
  unsigned long uploads_before, uploads_after, submits;
  GpuBuffer buffers[kBuffers];
  int ok = 1;
  int i, pass;

  x2_log_info("\n=== gpu upload selftest: the frame's uploads share one "
              "staging page ===\n");
  if (!gpu_device_create()) {
    x2_log_info("gpu upload selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  memset(data, 0x5a, sizeof data);
  for (i = 0; i < kBuffers; i++) {
    buffers[i] = gpu_buffer_create(GPU_BUF_VERTEX, sizeof data);
    if (!buffers[i]) {
      x2_log_info("gpu upload selftest: FAILED -- no buffer.\n");
      gpu_device_destroy();
      return 1;
    }
  }
  gpu_draw_perf(&draw_ns, &upload_ns, &alloc_ns, &submit_ns, &allocs_before,
                &uploads_before, &submits);

  /*
   * Two frames, because the interesting failure is in the second. A page
   * whose used offset survives its frame looks full the moment it is reused,
   * and the ring quietly allocates another one every frame -- the exact cost
   * it exists to remove, with a counter that still reads one.
   */
  for (pass = 0; pass < 2; pass++) {
    int upload;
    for (upload = 0; upload < kUploadsEach; upload++) {
      for (i = 0; i < kBuffers; i++) {
        ok = ok && gpu_buffer_upload(buffers[i], 0, data, sizeof data);
      }
    }
    gpu_draw_perf(&draw_ns, &upload_ns, &alloc_ns, &submit_ns, &allocs_after,
                  &uploads_after, &submits);
    if (!pass) {
      allocs_second_frame = allocs_after;
    }
    gpu_upload_batch_flush(g_gpu);
  }
  for (i = 0; i < kBuffers; i++) {
    gpu_buffer_destroy(buffers[i]);
  }
  gpu_device_destroy();

  if (!ok || uploads_after - uploads_before != kBuffers * kUploadsEach * 2 ||
      allocs_second_frame - allocs_before != 1 ||
      allocs_after != allocs_second_frame) {
    x2_log_info(
        "gpu upload selftest: FAILED -- %lu upload(s) took %llu "
        "staging allocation(s) in the first frame and %llu more in "
        "the second; expected %d upload(s), one page, and no second "
        "allocation.\n",
        uploads_after - uploads_before, allocs_second_frame - allocs_before,
        allocs_after - allocs_second_frame, kBuffers * kUploadsEach * 2);
    return 1;
  }
  x2_log_info("gpu upload selftest: PASSED -- %lu upload(s) across two frames "
              "shared one staging page.\n",
              uploads_after - uploads_before);
  return 0;
}

/*
 * A dynamic buffer may be drawn, discarded, rewritten and drawn again before
 * Present. The two draws must retain the bytes that were current when each
 * draw was recorded. Submitting the rewrite ahead of the still-open frame
 * command buffer without cycling the destination makes both draws read the
 * second set of bytes instead.
 */
int gpu_upload_order_selftest(void) {
  struct Vertex {
    float x, y, z, rhw;
    unsigned color;
  };
  static const struct Vertex first[3] = {
      {2.0f, 2.0f, 0.5f, 1.0f, 0xFFFF0000u},
      {30.0f, 2.0f, 0.5f, 1.0f, 0xFFFF0000u},
      {16.0f, 62.0f, 0.5f, 1.0f, 0xFFFF0000u}};
  static const struct Vertex second[3] = {
      {34.0f, 2.0f, 0.5f, 1.0f, 0xFF00FF00u},
      {62.0f, 2.0f, 0.5f, 1.0f, 0xFF00FF00u},
      {48.0f, 62.0f, 0.5f, 1.0f, 0xFF00FF00u}};
  static unsigned pixels[64 * 64];
  GpuBuffer buffer;
  GpuDraw draw;
  int ok;

  x2_log_info("\n=== gpu upload-order selftest: draw, discard, draw retains "
              "both buffer generations ===\n");
  if (!gpu_device_create()) {
    x2_log_info("gpu upload-order selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  buffer = gpu_buffer_create(GPU_BUF_VERTEX, sizeof first);
  memset(&draw, 0, sizeof draw);
  draw.vertices = buffer;
  draw.vertex_stride = sizeof first[0];
  draw.prim = GPU_PRIM_TRIANGLELIST;
  draw.prim_count = 1;
  draw.pos_offset = 0;
  draw.pretransformed = 1;
  draw.color_offset = 16;
  draw.uv_offset = -1;
  draw.normal_offset = -1;
  draw.texop = GPU_TEXOP_NONE;
  draw.cull = GPU_CULL_NONE;
  draw.depth_func = GPU_CMP_ALWAYS;

  ok = buffer && gpu_buffer_upload(buffer, 0, first, sizeof first) &&
       gpu_offscreen_begin(64, 64, 0.0f, 0.0f, 1.0f, 1.0f) && gpu_draw(&draw) &&
       gpu_buffer_upload(buffer, 0, second, sizeof second) && gpu_draw(&draw) &&
       gpu_offscreen_read(pixels, sizeof pixels);
  gpu_offscreen_end();
  if (buffer)
    gpu_buffer_destroy(buffer);
  gpu_device_destroy();

  if (!ok || pixels[32u * 64u + 16u] != 0xFFFF0000u ||
      pixels[32u * 64u + 48u] != 0xFF00FF00u) {
    x2_log_info("gpu upload-order selftest: FAILED -- left pixel 0x%08x, "
                "right pixel 0x%08x; expected the first red draw and second "
                "green draw to coexist.\n",
                pixels[32u * 64u + 16u], pixels[32u * 64u + 48u]);
    return 1;
  }
  x2_log_info(
      "gpu upload-order selftest: PASSED -- the first red draw retained "
      "its buffer generation and the second used the green rewrite.\n");
  return 0;
}
