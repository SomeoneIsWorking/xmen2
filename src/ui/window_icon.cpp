#include "window_icon.hpp"

#include "ui_resources.h"
#include "x2_log.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace x2::ui {

WindowIcon::WindowIcon()
    : surface_(IMG_Load(ui_resource_path("app_icon.svg"))) {
  if (!surface_) {
    x2_log_error("window icon: %s: %s\n", ui_resource_path("app_icon.svg"),
                 SDL_GetError());
  }
}

WindowIcon::~WindowIcon() { SDL_DestroySurface(surface_); }

void WindowIcon::apply(SDL_Window *window) const {
  if (surface_ && !SDL_SetWindowIcon(window, surface_)) {
    x2_log_error("window icon: not applied: %s\n", SDL_GetError());
  }
}

} // namespace x2::ui
