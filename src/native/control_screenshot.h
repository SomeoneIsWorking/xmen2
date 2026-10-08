/* Control-channel screenshot state, separate from HTTP and GPU ownership. */
#pragma once

#include <cstddef>

namespace x2::native {

struct ControlScreenshot {
  int armed;
  unsigned char *png;
  std::size_t png_bytes;
  unsigned width;
  unsigned height;
};

inline constexpr int kControlScreenshotFailed = -1;
inline constexpr int kControlScreenshotPending = 0;
inline constexpr int kControlScreenshotReady = 1;

/* Arm once, then poll from subsequent render-frame completion points. */
int control_screenshot_poll(ControlScreenshot *shot, char *why, int whyn);

/* Cancel a timed-out HTTP request on the render thread. */
void control_screenshot_abandon(ControlScreenshot *shot);

const unsigned char *control_screenshot_png(const ControlScreenshot *shot,
                                            std::size_t *bytes);

} // namespace x2::native
