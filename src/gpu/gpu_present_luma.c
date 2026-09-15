/* See gpu_present_luma.h. */
#include "gpu_present_luma.h"

#include "../native/x2_log.h"
#include "gpu_capture.h"

#include <lucent/cvar_c.h>

#include <stdio.h>
#include <stdlib.h>

/* A full-frame GPU download per sample is expensive; the probe exists for
   measurement runs, not for every frame. Sampling is strided so the stats
   cost stays bounded at 4K pixels regardless of resolution. */
#define LUMA_SAMPLES 4096u
#define NONBLACK_CHANNEL 24u

void x2_present_luma_stats(const unsigned char *bgra, uint32_t width,
                           uint32_t height, X2PresentLumaStats *out) {
  uint64_t pixels, stride, i;
  uint64_t luma_sum = 0;

  out->mean_luma = 0.0;
  out->max_channel = 0;
  out->sampled = 0;
  out->nonblack = 0;
  if (!bgra || !width || !height)
    return;
  pixels = (uint64_t)width * (uint64_t)height;
  stride = pixels / LUMA_SAMPLES;
  if (stride < 1)
    stride = 1;
  for (i = 0; i < pixels; i += stride) {
    const unsigned char *px = bgra + i * 4u; /* BGRA */
    unsigned char b = px[0], g = px[1], r = px[2];
    uint32_t channel_max = r > g ? r : g;
    if (b > channel_max)
      channel_max = b;
    luma_sum +=
        ((uint64_t)r * 77u + (uint64_t)g * 150u + (uint64_t)b * 28u) >> 8;
    if (channel_max > out->max_channel)
      out->max_channel = channel_max;
    if (channel_max > NONBLACK_CHANNEL)
      out->nonblack++;
    out->sampled++;
  }
  if (out->sampled)
    out->mean_luma = (double)luma_sum / (double)out->sampled;
}

#ifdef X2_WITH_SDL
/* The frame driver needs the capture owner, which needs SDL. */

#include "gpu_present.h"
#include "gpu_readback.h"

#include <SDL3/SDL_gpu.h>

/* A capture completes a present or two after its request (the next frame end
   selects the retained target and downloads; the harvest finds READY). So a
   request this many presents old that still has no result is a stalled state
   machine, and the probe says so rather than going quiet. */
#define LUMA_STALL_PRESENTS 120ul

static double pct_nonblack(const X2PresentLumaStats *s) {
  return s->sampled ? 100.0 * (double)s->nonblack / (double)s->sampled : 0.0;
}

void x2_present_luma_frame(SDL_GPUDevice *device, unsigned long draws) {
  static unsigned long presents;
  static long every = -1;
  static int outstanding; /* requested, not yet harvested */
  static unsigned long requested_at;
  static int said_request, said_failure, said_stall;
  static unsigned char *scene_buf;
  static uint32_t scene_capacity;
  static char failure[192];
  const unsigned char *bgra;
  X2PresentLumaStats composed;
  X2PresentLumaStats scene;
  SDL_GPUTexture *scene_texture;
  uint32_t width, height;
  uint32_t scene_w = 0, scene_h = 0;
  const char *scene_state;
  int rc;

  presents++;
  if (every < 0) {
    every = lucent_cvar_number("present_luma", 0);
    if (every < 0)
      every = 0;
    if (every)
      x2_log_info("present_luma: armed -- every %ld windowed present(s) one "
                  "frame is GPU-downloaded and its luma logged. A run that "
                  "prints no [LUMA] line never reached a windowed present.\n",
                  every);
  }
  if (!every)
    return;

  if (outstanding) {
    /* Only ask the capture owner when THIS probe asked it for a frame.
       Asking cold returns "no screenshot has been requested" -- the idle
       state, not a failure -- and the first version of this driver mistook
       that for one and silenced itself on the first present. */
    rc = gpu_capture_result(&bgra, &width, &height, failure, sizeof failure);
    if (rc == 1) {
      x2_present_luma_stats(bgra, width, height, &composed);
      scene.mean_luma = 0.0;
      scene.max_channel = 0;
      scene.sampled = 0;
      scene.nonblack = 0;
      scene_state = "none";
      scene_texture = gpu_present_scene(device, &scene_w, &scene_h);
      if (scene_texture && scene_w && scene_h) {
        uint64_t need = (uint64_t)scene_w * (uint64_t)scene_h * 4u;
        if (need <= (uint64_t)UINT32_MAX && need > (uint64_t)scene_capacity) {
          unsigned char *larger = realloc(scene_buf, (size_t)need);
          if (larger) {
            scene_buf = larger;
            scene_capacity = (uint32_t)need;
          }
        }
        if (scene_buf && need <= (uint64_t)scene_capacity)
          scene_state =
              gpu_readback_texture_rgba(device, scene_texture, scene_w, scene_h,
                                        scene_buf, scene_capacity)
                  ? "read"
                  : "unreadable";
        else
          scene_state = "no-buffer";
        if (scene_state[0] == 'r' && scene_state[1] == 'e')
          x2_present_luma_stats(scene_buf, scene_w, scene_h, &scene);
      }
      x2_log_info("[LUMA] present %lu: composed mean %.1f max %u nonblack "
                  "%.1f%% | scene %s mean %.1f max %u nonblack %.1f%% "
                  "(frame %ux%u, scene %ux%u, %lu draw(s) so far)\n",
                  presents, composed.mean_luma, composed.max_channel,
                  pct_nonblack(&composed), scene_state, scene.mean_luma,
                  scene.max_channel, pct_nonblack(&scene), width, height,
                  scene_w, scene_h, draws);
      gpu_capture_discard();
      outstanding = 0;
      return;
    }
    if (rc == -1) {
      gpu_capture_discard();
      outstanding = 0;
      if (!said_failure++)
        x2_log_error("[LUMA] the capture this probe armed failed: %s -- it "
                     "keeps trying every %ld presents.\n",
                     failure, every);
      return;
    }
    if (presents - requested_at > LUMA_STALL_PRESENTS && !said_stall++)
      x2_log_error("[LUMA] no capture completed within %lu windowed "
                   "presents of its request -- the frame never reached the "
                   "retained-target swap. Reported once.\n",
                   (unsigned long)LUMA_STALL_PRESENTS);
    return;
  }

  if ((presents % (unsigned long)every) == 0) {
    if (gpu_capture_request(failure, sizeof failure)) {
      outstanding = 1;
      requested_at = presents;
    } else if (!said_request++) {
      x2_log_error("[LUMA] the probe could not arm a capture: %s -- it keeps "
                   "trying every %ld presents.\n",
                   failure, every);
    }
  }
}
#endif
