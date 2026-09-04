/* Asset-free proof of the host frame lifecycle, isolated from draw tests. */
#include "../native/x2_log.h"
#include "gpu_device.h"

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define FRAMES 3

int gpu_device_selftest(void) {
#ifndef X2_WITH_SDL
  x2_log_info("gpu selftest: SKIPPED -- built without SDL, so there is no "
              "device to test. This is not a pass.\n");
  return 77;
#else
  SDL_Window *w = NULL;
  int i, presented_ok = 0, began = 0;

  x2_log_info("\n=== gpu selftest: the host frame path, with no engine "
              "involved ===\n");
  if (!gpu_device_create()) {
    x2_log_info("gpu selftest: FAILED -- no GPU device. Nothing below could "
                "have run.\n");
    return 1;
  }

  /* A visible owned window is required: hidden SDL windows have no swapchain.
   */
  w = SDL_CreateWindow("igvk selftest", 320, 240, 0);
  if (!w) {
    x2_log_info(
        "gpu selftest: SKIPPED -- no window could be created (%s), so "
        "there is no surface to present to. The GPU device WAS created, "
        "so this is the environment, not the renderer. Nothing below "
        "was checked.\n",
        SDL_GetError());
    gpu_device_destroy();
    return 77;
  }
  if (!gpu_device_attach_window(w)) {
    x2_log_info("gpu selftest: FAILED -- the swapchain could not be claimed "
                "on a window that was created successfully.\n");
    SDL_DestroyWindow(w);
    gpu_device_destroy();
    return 1;
  }

  for (i = 0; i < FRAMES; i++) {
    if (!gpu_frame_begin())
      continue;
    began++;
    gpu_frame_clear(1u, i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f,
                    i == 2 ? 1.0f : 0.0f, 1.0f, 1.0f, 0u);
    gpu_frame_viewport(0, 0, 320, 240, 0.0f, 1.0f);
    if (!gpu_frame_in_progress()) {
      x2_log_info("gpu selftest: FAILED -- frame %d reported no frame in "
                  "progress between begin and end.\n",
                  i);
      break;
    }
    gpu_frame_end();
    if (gpu_frame_in_progress()) {
      x2_log_info("gpu selftest: FAILED -- frame %d was still open after "
                  "gpu_frame_end.\n",
                  i);
      break;
    }
    presented_ok++;
  }

  gpu_device_report();
  gpu_device_attach_window(NULL);
  SDL_DestroyWindow(w);
  gpu_device_destroy();
  if (presented_ok != FRAMES) {
    x2_log_info("gpu selftest: FAILED -- %d of %d frames completed (%d were "
                "begun). A frame path that cannot present in isolation will "
                "not present under the engine either.\n",
                presented_ok, FRAMES, began);
    return 1;
  }
  x2_log_info(
      "gpu selftest: PASSED -- %d frames acquired, cleared and presented.\n"
      "  This proves the HOST half only. Whether the engine's own order of\n"
      "  beginDraw / clearRenderDestination / setViewport / endDraw produces\n"
      "  the right picture is not tested here.\n",
      presented_ok);
  return 0;
#endif
}
