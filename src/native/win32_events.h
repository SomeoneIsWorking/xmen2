#pragma once

#include <cstdint>

struct SDL_Window;

namespace x2::native {

/* Attach/detach the guest's one window to the SDL event bridge. Detaching
   restores the OS cursor before the window disappears. */
void win32_events_window(SDL_Window *window, std::uint32_t hwnd, int hidden);
void win32_events_hide_window(int hidden);

/* The registered class procedure becomes the window procedure at creation;
   SetWindowLong(GWL_WNDPROC) may replace it afterward. */
void win32_events_register_wndproc(std::uint32_t wndproc);
std::uint32_t win32_events_registered_wndproc();
void win32_events_set_wndproc(std::uint32_t wndproc);

void win32_events_modal(int visible);
int win32_events_guest_show_cursor(int show);

/* Guest screen coordinates keep the host window origin, but measure offsets
   from it in logical game pixels. This lets the retained Win32 code compose
   ClientToScreen/GetCursorPos/SetCursorPos while this bridge owns the one
   physical<->logical aspect-fit transform. */
int win32_events_client_to_screen(std::int32_t *x, std::int32_t *y);
int win32_events_screen_to_client(std::int32_t *x, std::int32_t *y);
int win32_events_get_cursor_pos(std::int32_t *x, std::int32_t *y);
int win32_events_set_cursor_pos(std::int32_t x, std::int32_t y);

} // namespace x2::native
