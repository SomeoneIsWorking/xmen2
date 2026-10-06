/*
 * Is the WINDOW actually showing something?
 *
 * The headless `X2_SHOT` route photographs the headless target and refuses a
 * run with a real window, and a page-level screenshot of an OffscreenCanvas
 * that a worker owns cannot be trusted to tell compositor-black from
 * game-black. So a browser run that "captured black" had never been measured
 * by an in-engine instrument on the windowed present path.
 *
 * This probe closes that hole with the shipping capture owner's own state
 * machine (`gpu_capture_request`/`gpu_capture_result`, the path control
 * `/shot` serves from): every N windowed presents it photographs the frame
 * that is about to reach the screen and logs its luma, with the draw count
 * beside it so "black but geometry drew" separates from "black and nothing
 * was submitted". It is a diagnostic: armed by the registered CVar
 * `present_luma=<N>` (`--set present_luma=N`, reachable from the browser page
 * through `?arg=`), and silent when unarmed.
 */
#ifndef X2_GPU_PRESENT_LUMA_H
#define X2_GPU_PRESENT_LUMA_H

#include <stdint.h>

/* Frame statistics over sampled pixels. `max_channel` is the largest single
   R/G/B byte seen, so a coloured-black failure (one channel alive) is not
   averaged away by the luma weights. */
typedef struct {
  double mean_luma; /* ITU-R weighted, 0..255, over the samples */
  uint32_t max_channel;
  uint32_t sampled;
  uint32_t nonblack; /* samples with any channel above 24 */
} X2PresentLumaStats;

/* Pure: the both-answers control is unit-testable without a GPU. */
void x2_present_luma_stats(const unsigned char *bgra, uint32_t width,
                           uint32_t height, X2PresentLumaStats *out);

/* Drive one windowed present: request, or harvest and report, per the armed
   interval. `draws` is the frame-counter denominator for the log line; the
   line also states the present TOPOLOGY (whether a logical D3D backbuffer
   scene exists, and at what size), because "the presented frame is black"
   reads completely differently when the scene stage is missing than when it
   exists and composites black. */
struct SDL_GPUDevice;
void x2_present_luma_frame(struct SDL_GPUDevice *device, unsigned long draws);

#endif /* X2_GPU_PRESENT_LUMA_H */
