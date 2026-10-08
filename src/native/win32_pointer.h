#pragma once

#include "win32_mouse.h"

#include <SDL3/SDL.h>
#include <cstdint>

struct X2TouchPointer;

namespace x2::native {

void win32_pointer_window(SDL_Window *window);
int win32_pointer_client_to_screen(int32_t *x, int32_t *y);
int win32_pointer_screen_to_client(int32_t *x, int32_t *y);
int win32_pointer_get_cursor_pos(int32_t *x, int32_t *y);
int win32_pointer_set_cursor_pos(int32_t x, int32_t y);
void win32_pointer_translate_mouse(const SDL_Event *event, Win32Mouse *mouse,
                                   uint32_t hwnd);
void win32_pointer_translate_touch(const X2TouchPointer *pointer,
                                   Win32Mouse *mouse, uint32_t hwnd);

} // namespace x2::native
