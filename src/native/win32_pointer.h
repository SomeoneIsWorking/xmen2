#ifndef X2_WIN32_POINTER_H
#define X2_WIN32_POINTER_H

#include "win32_mouse.h"

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct X2TouchPointer X2TouchPointer;

void x2_win32_pointer_window(SDL_Window *window);
int x2_win32_pointer_client_to_screen(int32_t *x, int32_t *y);
int x2_win32_pointer_screen_to_client(int32_t *x, int32_t *y);
int x2_win32_pointer_get_cursor_pos(int32_t *x, int32_t *y);
int x2_win32_pointer_set_cursor_pos(int32_t x, int32_t y);
void x2_win32_pointer_translate_mouse(const SDL_Event *event,
                                      X2Win32Mouse *mouse, uint32_t hwnd);
void x2_win32_pointer_translate_touch(const X2TouchPointer *pointer,
                                      X2Win32Mouse *mouse, uint32_t hwnd);

#endif
