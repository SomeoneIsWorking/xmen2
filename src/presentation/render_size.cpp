#include "render_size.h"

#include "display_geometry.h"
#include "resolution_ladder.h"

namespace x2::presentation {

bool output_is_display(x2::config::WindowMode mode) {
  return mode != x2::config::WindowMode::Windowed ||
         host_window_fills_display();
}

static bool surface_size(const x2::config::Settings *settings,
                         SDL_Window *window, unsigned *width,
                         unsigned *height) {
  if (!output_is_display(settings->window_mode))
    return false;
  return window_pixel_size(window, width, height) ||
         display_pixel_size(width, height);
}

int render_size_resolve(x2::config::Settings *settings, SDL_Window *window) {
  unsigned width = 0, height = 0;

  if (!settings)
    return 0;
  if (settings->render_lines == x2::config::kRenderNative) {
    if (!surface_size(settings, window, &width, &height))
      return 0;
  } else {
    unsigned shape_w = 0, shape_h = 0;
    if (!surface_size(settings, window, &shape_w, &shape_h))
      display_pixel_size(&shape_w, &shape_h);
    height = settings->render_lines;
    width = resolution_width_for(height, shape_w, shape_h);
  }
  if (width == settings->width && height == settings->height)
    return 0;
  settings->width = width;
  settings->height = height;
  return 1;
}

void render_size_window(const x2::config::Settings *settings, unsigned *width,
                        unsigned *height) {
  if (output_is_display(settings->window_mode) &&
      display_pixel_size(width, height))
    return;
  *width = settings->width;
  *height = settings->height;
}

} // namespace x2::presentation
