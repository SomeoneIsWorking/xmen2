#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::native {

/* The subset of USER32 messages produced by the SDL host. Keep the Win32
   values here so translation and its tests share one vocabulary. */
inline constexpr unsigned kWmActivate = 0x0006u;
inline constexpr unsigned kWmQuit = 0x0012u;
inline constexpr unsigned kWmMouseMove = 0x0200u;
inline constexpr unsigned kWmLButtonDown = 0x0201u;
inline constexpr unsigned kWmLButtonUp = 0x0202u;
inline constexpr unsigned kWmRButtonDown = 0x0204u;
inline constexpr unsigned kWmRButtonUp = 0x0205u;
inline constexpr unsigned kWmMButtonDown = 0x0207u;
inline constexpr unsigned kWmMButtonUp = 0x0208u;

inline constexpr unsigned kWaInactive = 0u;
inline constexpr unsigned kWaActive = 1u;

inline constexpr unsigned kMkLButton = 0x0001u;
inline constexpr unsigned kMkRButton = 0x0002u;
inline constexpr unsigned kMkShift = 0x0004u;
inline constexpr unsigned kMkControl = 0x0008u;
inline constexpr unsigned kMkMButton = 0x0010u;

inline constexpr unsigned kWin32MessageCapacity = 64u;

enum class Win32MouseButton { Left, Right, Middle };

struct Win32Message {
  uint32_t hwnd;
  uint32_t message;
  uint32_t wparam;
  uint32_t lparam;
  uint32_t time;
  int32_t screen_x;
  int32_t screen_y;
};

struct Win32Mouse {
  Win32Message messages[kWin32MessageCapacity];
  size_t message_count;
  uint32_t buttons;
  int quit_posted;
  int window_hidden;
  int window_focused;
  int pointer_inside;
  int overlay_visible;
  int modal_visible;
  int guest_cursor_count;
};

/* Queue an ordinary thread/window message. A full queue is reported to the
   caller instead of silently dropping input. */
int win32_message_post(Win32Mouse *mouse, const Win32Message *message);
int win32_message_post_quit(Win32Mouse *mouse);

/* PeekMessage/GetMessage's shared selection rule. WM_QUIT bypasses filters as
   it does on Win32. `remove` chooses PM_NOREMOVE versus PM_REMOVE. */
int win32_message_take(Win32Mouse *mouse, uint32_t hwnd, uint32_t filter_min,
                       uint32_t filter_max, int remove, Win32Message *message);

/* Map host-window coordinates back through the same aspect-fit rectangle used
   for presentation. The result is the logical game backbuffer coordinate
   carried in a mouse message; Y stays top-down because Alchemy performs its
   own one-and-only inversion. */
int win32_mouse_map_point(int32_t x, int32_t y, uint32_t window_width,
                          uint32_t window_height, uint32_t game_width,
                          uint32_t game_height, int32_t *game_x,
                          int32_t *game_y);
/* The inverse used for guest-requested cursor warps. A logical coordinate is
   placed at the centre of its fitted host-coordinate bucket where
   representable, so mapping the resulting host point returns the same logical
   point. */
int win32_mouse_unmap_point(int32_t game_x, int32_t game_y,
                            uint32_t window_width, uint32_t window_height,
                            uint32_t game_width, uint32_t game_height,
                            int32_t *x, int32_t *y);
uint32_t win32_mouse_pack_point(int32_t x, int32_t y);

int win32_mouse_motion(Win32Mouse *mouse, uint32_t hwnd, int32_t client_x,
                       int32_t client_y, int32_t screen_x, int32_t screen_y,
                       uint32_t time, uint32_t buttons, uint32_t modifiers);
int win32_mouse_button(Win32Mouse *mouse, uint32_t hwnd,
                       Win32MouseButton button, int down, int32_t client_x,
                       int32_t client_y, int32_t screen_x, int32_t screen_y,
                       uint32_t time, uint32_t modifiers);

/* Product cursor arbitration. The game-drawn cursor owns focused retail
   content; the OS cursor owns unfocused/outside content and native UI. */
void win32_mouse_window_state(Win32Mouse *mouse, int hidden, int focused,
                              int pointer_inside);
void win32_mouse_overlay(Win32Mouse *mouse, int visible);
void win32_mouse_modal(Win32Mouse *mouse, int visible);
int win32_mouse_os_cursor_visible(const Win32Mouse *mouse);

/* Preserve USER32's process-wide display counter and return value without
   letting it override the one-cursor product policy above. */
int win32_mouse_guest_show_cursor(Win32Mouse *mouse, int show);

} // namespace x2::native
