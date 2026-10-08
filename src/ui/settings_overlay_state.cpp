#include "settings_overlay_state.h"

/* SDLK_F2: SDL3 keycodes are 1<<30 | scancode, and F2 is scancode 59. This
   owner stays free of SDL headers; tests/test_settings_overlay.cpp pins the
   constant against SDL's own definition so it cannot drift. */
#define X2_OVERLAY_TOGGLE_KEY 0x4000003b

namespace x2::ui {

static int g_visible;

void settings_overlay_show(void) { g_visible = 1; }

void settings_overlay_hide(void) { g_visible = 0; }

int settings_overlay_visible(void) { return g_visible; }

int settings_overlay_toggle_key(int keycode, int is_down, int repeat) {
  if (keycode != X2_OVERLAY_TOGGLE_KEY || !is_down || repeat)
    return 0;
  g_visible = !g_visible;
  return 1;
}

} // namespace x2::ui
