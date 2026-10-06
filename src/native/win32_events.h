#ifndef X2_WIN32_EVENTS_H
#define X2_WIN32_EVENTS_H

#include <stdint.h>

struct SDL_Window;

/* Attach/detach the guest's one window to the SDL event bridge. Detaching
   restores the OS cursor before the window disappears. */
void x2_win32_events_window(struct SDL_Window *window, uint32_t hwnd,
                            int hidden);
void x2_win32_events_hide_window(int hidden);

/* The registered class procedure becomes the window procedure at creation;
   SetWindowLong(GWL_WNDPROC) may replace it afterward. */
void x2_win32_events_register_wndproc(uint32_t wndproc);
uint32_t x2_win32_events_registered_wndproc(void);
void x2_win32_events_set_wndproc(uint32_t wndproc);

void x2_win32_events_modal(int visible);
int x2_win32_events_guest_show_cursor(int show);

/* Guest screen coordinates keep the host window origin, but measure offsets
   from it in logical game pixels. This lets the retained Win32 code compose
   ClientToScreen/GetCursorPos/SetCursorPos while this bridge owns the one
   physical<->logical aspect-fit transform. */
int x2_win32_events_client_to_screen(int32_t *x, int32_t *y);
int x2_win32_events_screen_to_client(int32_t *x, int32_t *y);
int x2_win32_events_get_cursor_pos(int32_t *x, int32_t *y);
int x2_win32_events_set_cursor_pos(int32_t x, int32_t y);

#endif
