#include "win32_mouse.h"

#include "aspect_fit.h"

#include <limits.h>
#include <string.h>

namespace x2::native {

namespace {

int message_matches(const Win32Message *message, uint32_t hwnd,
                    uint32_t filter_min, uint32_t filter_max) {
  if (message->message == kWmQuit)
    return 1;
  if (hwnd == UINT32_MAX) {
    if (message->hwnd != 0u)
      return 0;
  } else if (hwnd != 0u && message->hwnd != hwnd) {
    return 0;
  }
  if (filter_min == 0u && filter_max == 0u)
    return 1;
  return message->message >= filter_min && message->message <= filter_max;
}

} // namespace

int win32_message_post(Win32Mouse *mouse, const Win32Message *message) {
  if (!mouse || !message || mouse->message_count >= kWin32MessageCapacity)
    return 0;
  mouse->messages[mouse->message_count++] = *message;
  return 1;
}

int win32_message_post_quit(Win32Mouse *mouse) {
  Win32Message message;

  if (!mouse)
    return 0;
  if (mouse->quit_posted)
    return 1;
  memset(&message, 0, sizeof message);
  message.message = kWmQuit;
  if (!win32_message_post(mouse, &message))
    return 0;
  mouse->quit_posted = 1;
  return 1;
}

int win32_message_take(Win32Mouse *mouse, uint32_t hwnd, uint32_t filter_min,
                       uint32_t filter_max, int remove, Win32Message *message) {
  size_t i;

  if (!mouse || !message)
    return 0;
  for (i = 0; i < mouse->message_count; i++) {
    if (!message_matches(&mouse->messages[i], hwnd, filter_min, filter_max))
      continue;
    *message = mouse->messages[i];
    if (remove) {
      if (message->message == kWmQuit)
        mouse->quit_posted = 0;
      memmove(&mouse->messages[i], &mouse->messages[i + 1u],
              (mouse->message_count - i - 1u) * sizeof *message);
      mouse->message_count--;
    }
    return 1;
  }
  return 0;
}

namespace {

int32_t map_axis(int32_t coordinate, uint32_t offset, uint32_t fitted_extent,
                 uint32_t game_extent) {
  int64_t relative = (int64_t)coordinate - offset;

  if (relative <= 0)
    return 0;
  if ((uint64_t)relative >= fitted_extent)
    return (int32_t)game_extent - 1;
  return (int32_t)((uint64_t)relative * game_extent / fitted_extent);
}

} // namespace

namespace {

int32_t unmap_axis(int32_t coordinate, uint32_t offset, uint32_t fitted_extent,
                   uint32_t game_extent) {
  uint32_t logical;
  uint64_t host;

  if (coordinate <= 0)
    logical = 0;
  else if ((uint32_t)coordinate >= game_extent)
    logical = game_extent - 1u;
  else
    logical = (uint32_t)coordinate;
  host = ((uint64_t)logical * 2u + 1u) * fitted_extent /
         ((uint64_t)game_extent * 2u);
  if (host >= fitted_extent)
    host = fitted_extent - 1u;
  return (int32_t)(offset + host);
}

} // namespace

int win32_mouse_map_point(int32_t x, int32_t y, uint32_t window_width,
                          uint32_t window_height, uint32_t game_width,
                          uint32_t game_height, int32_t *game_x,
                          int32_t *game_y) {
  X2AspectRect fitted;

  if (!game_x || !game_y || game_width > (uint32_t)INT16_MAX + 1u ||
      game_height > (uint32_t)INT16_MAX + 1u ||
      !x2_aspect_fit(window_width, window_height, game_width, game_height,
                     &fitted))
    return 0;
  *game_x = map_axis(x, fitted.x, fitted.width, game_width);
  *game_y = map_axis(y, fitted.y, fitted.height, game_height);
  return 1;
}

int win32_mouse_unmap_point(int32_t game_x, int32_t game_y,
                            uint32_t window_width, uint32_t window_height,
                            uint32_t game_width, uint32_t game_height,
                            int32_t *x, int32_t *y) {
  X2AspectRect fitted;

  if (!x || !y || window_width > (uint32_t)INT32_MAX ||
      window_height > (uint32_t)INT32_MAX ||
      game_width > (uint32_t)INT16_MAX + 1u ||
      game_height > (uint32_t)INT16_MAX + 1u ||
      !x2_aspect_fit(window_width, window_height, game_width, game_height,
                     &fitted))
    return 0;
  *x = unmap_axis(game_x, fitted.x, fitted.width, game_width);
  *y = unmap_axis(game_y, fitted.y, fitted.height, game_height);
  return 1;
}

uint32_t win32_mouse_pack_point(int32_t x, int32_t y) {
  return (uint16_t)x | ((uint32_t)(uint16_t)y << 16u);
}

int win32_mouse_motion(Win32Mouse *mouse, uint32_t hwnd, int32_t client_x,
                       int32_t client_y, int32_t screen_x, int32_t screen_y,
                       uint32_t time, uint32_t buttons, uint32_t modifiers) {
  Win32Message message;

  if (!mouse)
    return 0;
  mouse->buttons = buttons & (kMkLButton | kMkRButton | kMkMButton);
  message.hwnd = hwnd;
  message.message = kWmMouseMove;
  message.wparam = mouse->buttons | modifiers;
  message.lparam = win32_mouse_pack_point(client_x, client_y);
  message.time = time;
  message.screen_x = screen_x;
  message.screen_y = screen_y;
  /* Win32 coalesces adjacent WM_MOUSEMOVE traffic. Preserve ordering around
     buttons/activation while preventing a high-rate SDL mouse from filling
     a queue the retail loop consumes one message at a time. */
  if (mouse->message_count != 0u &&
      mouse->messages[mouse->message_count - 1u].hwnd == hwnd &&
      mouse->messages[mouse->message_count - 1u].message == kWmMouseMove) {
    mouse->messages[mouse->message_count - 1u] = message;
    return 1;
  }
  return win32_message_post(mouse, &message);
}

namespace {

int button_messages(Win32MouseButton button, int down, uint32_t *flag,
                    uint32_t *message) {
  switch (button) {
  case Win32MouseButton::Left:
    *flag = kMkLButton;
    *message = down ? kWmLButtonDown : kWmLButtonUp;
    return 1;
  case Win32MouseButton::Right:
    *flag = kMkRButton;
    *message = down ? kWmRButtonDown : kWmRButtonUp;
    return 1;
  case Win32MouseButton::Middle:
    *flag = kMkMButton;
    *message = down ? kWmMButtonDown : kWmMButtonUp;
    return 1;
  }
  return 0;
}

} // namespace

int win32_mouse_button(Win32Mouse *mouse, uint32_t hwnd,
                       Win32MouseButton button, int down, int32_t client_x,
                       int32_t client_y, int32_t screen_x, int32_t screen_y,
                       uint32_t time, uint32_t modifiers) {
  Win32Message event;
  uint32_t flag, message;

  if (!mouse || !button_messages(button, down, &flag, &message))
    return 0;
  if (down)
    mouse->buttons |= flag;
  else
    mouse->buttons &= ~flag;
  event.hwnd = hwnd;
  event.message = message;
  event.wparam = mouse->buttons | modifiers;
  event.lparam = win32_mouse_pack_point(client_x, client_y);
  event.time = time;
  event.screen_x = screen_x;
  event.screen_y = screen_y;
  return win32_message_post(mouse, &event);
}

void win32_mouse_window_state(Win32Mouse *mouse, int hidden, int focused,
                              int pointer_inside) {
  if (!mouse)
    return;
  mouse->window_hidden = hidden != 0;
  mouse->window_focused = focused != 0;
  mouse->pointer_inside = pointer_inside != 0;
}

void win32_mouse_overlay(Win32Mouse *mouse, int visible) {
  if (mouse)
    mouse->overlay_visible = visible != 0;
}

void win32_mouse_modal(Win32Mouse *mouse, int visible) {
  if (mouse)
    mouse->modal_visible = visible != 0;
}

int win32_mouse_os_cursor_visible(const Win32Mouse *mouse) {
  if (!mouse || mouse->window_hidden)
    return 0;
  return !mouse->window_focused || !mouse->pointer_inside ||
         mouse->overlay_visible || mouse->modal_visible;
}

int win32_mouse_guest_show_cursor(Win32Mouse *mouse, int show) {
  if (!mouse)
    return 0;
  mouse->guest_cursor_count += show ? 1 : -1;
  return mouse->guest_cursor_count;
}

} // namespace x2::native
