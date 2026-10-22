#pragma once

struct SDL_Surface;
struct SDL_Window;

namespace x2::ui {

/* The application icon (ui/app_icon.svg), rasterized for the windows the port
 * opens. A window carries its own icon because an AppImage that was never
 * installed has no desktop entry for the desktop to look it up by. */
class WindowIcon {
public:
  WindowIcon();
  ~WindowIcon();
  WindowIcon(const WindowIcon &) = delete;
  WindowIcon &operator=(const WindowIcon &) = delete;

  /* Null when the shipped icon could not be loaded; the reason is logged. */
  SDL_Surface *surface() const noexcept { return surface_; }
  void apply(SDL_Window *window) const;

private:
  SDL_Surface *surface_ = nullptr;
};

} // namespace x2::ui
