#include "win32_pointer.h"
#include "x2_log.h"

#include "../input/touch_runtime.h"
#include "settings_store.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  int32_t window_x;
  int32_t window_y;
  uint32_t window_width;
  uint32_t window_height;
  uint32_t game_width;
  uint32_t game_height;
} MouseGeometry;

static SDL_Window *g_window;

void x2_win32_pointer_window(SDL_Window *window) { g_window = window; }

static void mouse_geometry(MouseGeometry *geometry) {
  const X2Settings *settings = x2_settings_store();
  int window_x, window_y, window_width, window_height;

  if (!g_window) {
    x2_log_error(
        "win32 pointer: cursor coordinates require an attached guest window\n");
    abort();
  }
  if (!SDL_GetWindowPosition(g_window, &window_x, &window_y) ||
      !SDL_GetWindowSize(g_window, &window_width, &window_height)) {
    x2_log_error("win32 pointer: cannot read guest window geometry: %s\n",
                 SDL_GetError());
    abort();
  }
  if (window_width <= 0 || window_height <= 0) {
    x2_log_error("win32 pointer: guest window has invalid size %dx%d\n",
                 window_width, window_height);
    abort();
  }
  geometry->window_x = window_x;
  geometry->window_y = window_y;
  geometry->window_width = (uint32_t)window_width;
  geometry->window_height = (uint32_t)window_height;
  geometry->game_width = settings->width;
  geometry->game_height = settings->height;
}

static int32_t coordinate_add(int32_t coordinate, int64_t offset,
                              const char *axis) {
  int64_t result = (int64_t)coordinate + offset;

  if (result < INT32_MIN || result > INT32_MAX) {
    x2_log_error(
        "win32 pointer: %s cursor coordinate is outside the guest 32-bit "
        "range\n",
        axis);
    abort();
  }
  return (int32_t)result;
}

int x2_win32_pointer_client_to_screen(int32_t *x, int32_t *y) {
  MouseGeometry geometry;

  if (!x || !y)
    return 0;
  mouse_geometry(&geometry);
  *x = coordinate_add(*x, geometry.window_x, "horizontal");
  *y = coordinate_add(*y, geometry.window_y, "vertical");
  return 1;
}

int x2_win32_pointer_screen_to_client(int32_t *x, int32_t *y) {
  MouseGeometry geometry;

  if (!x || !y)
    return 0;
  mouse_geometry(&geometry);
  *x = coordinate_add(*x, -(int64_t)geometry.window_x, "horizontal");
  *y = coordinate_add(*y, -(int64_t)geometry.window_y, "vertical");
  return 1;
}

static void map_point(float host_x, float host_y, int32_t *client_x,
                      int32_t *client_y, int32_t *screen_x, int32_t *screen_y) {
  MouseGeometry geometry;
  int32_t x = (int32_t)host_x;
  int32_t y = (int32_t)host_y;

  mouse_geometry(&geometry);
  if (!x2_win32_mouse_map_point(x, y, geometry.window_width,
                                geometry.window_height, geometry.game_width,
                                geometry.game_height, client_x, client_y)) {
    x2_log_error(
        "win32 pointer: cannot map (%d,%d) from window %ux%u to game %ux%u\n",
        x, y, geometry.window_width, geometry.window_height,
        geometry.game_width, geometry.game_height);
    abort();
  }
  *screen_x = coordinate_add(*client_x, geometry.window_x, "horizontal");
  *screen_y = coordinate_add(*client_y, geometry.window_y, "vertical");
}

int x2_win32_pointer_get_cursor_pos(int32_t *x, int32_t *y) {
  MouseGeometry geometry;
  float global_x, global_y;
  int32_t client_x, client_y;

  if (!x || !y)
    return 0;
  mouse_geometry(&geometry);
  SDL_GetGlobalMouseState(&global_x, &global_y);
  if (!x2_win32_mouse_map_point(
          coordinate_add((int32_t)global_x, -(int64_t)geometry.window_x,
                         "horizontal"),
          coordinate_add((int32_t)global_y, -(int64_t)geometry.window_y,
                         "vertical"),
          geometry.window_width, geometry.window_height, geometry.game_width,
          geometry.game_height, &client_x, &client_y)) {
    x2_log_error(
        "win32 pointer: cannot map physical cursor to game coordinates\n");
    abort();
  }
  *x = coordinate_add(client_x, geometry.window_x, "horizontal");
  *y = coordinate_add(client_y, geometry.window_y, "vertical");
  return 1;
}

int x2_win32_pointer_set_cursor_pos(int32_t x, int32_t y) {
  MouseGeometry geometry;
  int32_t host_x, host_y;

  mouse_geometry(&geometry);
  if (!x2_win32_mouse_unmap_point(
          coordinate_add(x, -(int64_t)geometry.window_x, "horizontal"),
          coordinate_add(y, -(int64_t)geometry.window_y, "vertical"),
          geometry.window_width, geometry.window_height, geometry.game_width,
          geometry.game_height, &host_x, &host_y)) {
    x2_log_error(
        "win32 pointer: cannot map game cursor to window coordinates\n");
    abort();
  }
  host_x = coordinate_add(host_x, geometry.window_x, "horizontal");
  host_y = coordinate_add(host_y, geometry.window_y, "vertical");
  return SDL_WarpMouseGlobal((float)host_x, (float)host_y);
}

static uint32_t modifiers(void) {
  SDL_Keymod state = SDL_GetModState();
  uint32_t result = 0;

  if (state & SDL_KMOD_SHIFT)
    result |= X2_MK_SHIFT;
  if (state & SDL_KMOD_CTRL)
    result |= X2_MK_CONTROL;
  return result;
}

static uint32_t buttons(SDL_MouseButtonFlags state) {
  uint32_t result = 0;

  if (state & SDL_BUTTON_LMASK)
    result |= X2_MK_LBUTTON;
  if (state & SDL_BUTTON_RMASK)
    result |= X2_MK_RBUTTON;
  if (state & SDL_BUTTON_MMASK)
    result |= X2_MK_MBUTTON;
  return result;
}

static void require_queued(int queued, const char *kind) {
  if (queued)
    return;
  x2_log_error(
      "win32 pointer: ordered queue filled while posting %s; refusing to "
      "discard it\n",
      kind);
  abort();
}

void x2_win32_pointer_translate_mouse(const SDL_Event *event,
                                      X2Win32Mouse *mouse, uint32_t hwnd) {
  int32_t client_x, client_y, screen_x, screen_y;
  int queued;

  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    map_point(event->motion.x, event->motion.y, &client_x, &client_y, &screen_x,
              &screen_y);
    queued = x2_win32_mouse_motion(
        mouse, hwnd, client_x, client_y, screen_x, screen_y,
        (uint32_t)(event->motion.timestamp / 1000000u),
        buttons(event->motion.state), modifiers());
  } else {
    X2Win32MouseButton button;

    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->type != SDL_EVENT_MOUSE_BUTTON_UP)
      return;
    switch (event->button.button) {
    case SDL_BUTTON_LEFT:
      button = X2_WIN32_MOUSE_LEFT;
      break;
    case SDL_BUTTON_RIGHT:
      button = X2_WIN32_MOUSE_RIGHT;
      break;
    case SDL_BUTTON_MIDDLE:
      button = X2_WIN32_MOUSE_MIDDLE;
      break;
    default:
      return;
    }
    map_point(event->button.x, event->button.y, &client_x, &client_y, &screen_x,
              &screen_y);
    queued = x2_win32_mouse_button(
        mouse, hwnd, button, event->type == SDL_EVENT_MOUSE_BUTTON_DOWN,
        client_x, client_y, screen_x, screen_y,
        (uint32_t)(event->button.timestamp / 1000000u), modifiers());
  }
  require_queued(queued, "mouse event");
}

void x2_win32_pointer_translate_touch(const X2TouchPointer *pointer,
                                      X2Win32Mouse *mouse, uint32_t hwnd) {
  int32_t client_x, client_y, screen_x, screen_y;
  int queued;

  if (!pointer || !pointer->valid)
    return;
  map_point(pointer->x, pointer->y, &client_x, &client_y, &screen_x, &screen_y);
  queued =
      x2_win32_mouse_motion(mouse, hwnd, client_x, client_y, screen_x, screen_y,
                            pointer->time_ms, mouse->buttons, modifiers());
  if (queued && pointer->button_change >= 0)
    queued = x2_win32_mouse_button(
        mouse, hwnd, X2_WIN32_MOUSE_LEFT, pointer->button_change != 0, client_x,
        client_y, screen_x, screen_y, pointer->time_ms, modifiers());
  require_queued(queued, "touch portrait click");
}
