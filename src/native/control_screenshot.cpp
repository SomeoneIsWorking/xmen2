#include "control_screenshot.h"

#include "control_png.h"
#include "gpu_capture.h"

#include <stdio.h>
#include <stdlib.h>

namespace x2::native {

int control_screenshot_poll(ControlScreenshot *shot, char *why, int whyn) {
  const unsigned char *bgra = NULL;
  uint32_t width = 0, height = 0;
  int result;

  if (!shot) {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "the screenshot state owner is missing");
    return kControlScreenshotFailed;
  }
  if (!shot->armed) {
    if (!gpu_capture_request(why, whyn))
      return kControlScreenshotFailed;
    shot->armed = 1;
    return kControlScreenshotPending;
  }
  result = gpu_capture_result(&bgra, &width, &height, why, whyn);
  if (result == 0)
    return kControlScreenshotPending;
  shot->armed = 0;
  if (result < 0) {
    gpu_capture_discard();
    return kControlScreenshotFailed;
  }

  free(shot->png);
  shot->png = control_png_from_bgra(bgra, width, height, &shot->png_bytes);
  gpu_capture_discard();
  if (!shot->png) {
    shot->png_bytes = 0;
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn,
               "PNG encode of the bounded %ux%u capture failed", width, height);
    return kControlScreenshotFailed;
  }
  shot->width = width;
  shot->height = height;
  return kControlScreenshotReady;
}

void control_screenshot_abandon(ControlScreenshot *shot) {
  if (!shot || !shot->armed)
    return;
  gpu_capture_discard();
  shot->armed = 0;
}

const unsigned char *control_screenshot_png(const ControlScreenshot *shot,
                                            size_t *bytes) {
  if (bytes)
    *bytes = shot ? shot->png_bytes : 0;
  return shot ? shot->png : NULL;
}

} // namespace x2::native
