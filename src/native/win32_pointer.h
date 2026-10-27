#pragma once

#include "win32_mouse.h"

#include <SDL3/SDL.h>
#include <cstdint>

namespace x2::input {
struct TouchPointer;
} // namespace x2::input

namespace x2::native {

void win32_pointer_window(SDL_Window *window);
int win32_pointer_client_to_screen(int32_t *x, int32_t *y);
int win32_pointer_screen_to_client(int32_t *x, int32_t *y);
int win32_pointer_get_cursor_pos(int32_t *x, int32_t *y);
int win32_pointer_set_cursor_pos(int32_t x, int32_t y);
/* A guest client point as fractions of the window, where a finger lands on
   it; 0 when it maps outside the window. */
int win32_pointer_client_to_window(int32_t x, int32_t y, float *fraction_x,
                                   float *fraction_y);
void win32_pointer_translate_mouse(const SDL_Event *event, Win32Mouse *mouse,
                                   uint32_t hwnd);
/* A touch pointer event's position as a guest client point. */
void win32_pointer_touch_client(const input::TouchPointer *pointer,
                                int32_t *client_x, int32_t *client_y);

} // namespace x2::native
