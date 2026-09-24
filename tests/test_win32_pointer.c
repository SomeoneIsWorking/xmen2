/*
 * GetCursorPos ANSWERS WHERE THE EVENT STREAM PUT THE CURSOR.
 *
 * The game reads the cursor two ways: WM_MOUSEMOVE carries a position, and
 * GetCursorPos asks for one. Both must be the same point in the same game
 * coordinates, or a menu hovers one item and clicks another. This drives the
 * shipping win32_pointer.c through a real SDL window on the dummy video
 * driver: a warp moves SDL's cursor, the translation turns the resulting
 * motion into a message, and GetCursorPos must agree with it -- with the game
 * backbuffer a different size from the window, so the mapping is exercised
 * rather than the identity.
 */
#include "settings_store.h"
#include "win32_mouse.h"
#include "win32_pointer.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* No settings file: the store starts from defaults and this test sets the
   one field pair it depends on. */
const char *x2_save_dir(void) { return X2_TEST_WIN32_POINTER_ROOT; }

static int g_failures;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    g_failures++;
  }
}

/* Warp SDL's cursor to window point (x, y), translate what that produced,
   and compare the message's position with GetCursorPos's. */
static void cursor_agrees(SDL_Window *window, float x, float y,
                          const char *what) {
  X2Win32Mouse mouse;
  X2Win32Message message;
  SDL_Event event;
  int32_t cursor_x = 0, cursor_y = 0;
  int moved = 0;

  memset(&mouse, 0, sizeof mouse);
  SDL_WarpMouseInWindow(window, x, y);
  SDL_PumpEvents();
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      x2_win32_pointer_translate_mouse(&event, &mouse, 1u);
      moved = 1;
    }
  }
  check(moved, what);
  check(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &message) &&
            message.message == X2_WM_MOUSEMOVE,
        "the warp was translated into WM_MOUSEMOVE");
  check(x2_win32_pointer_get_cursor_pos(&cursor_x, &cursor_y),
        "GetCursorPos answers");
  if (cursor_x != message.screen_x || cursor_y != message.screen_y) {
    fprintf(stderr, "  %s: message (%d,%d), GetCursorPos (%d,%d)\n", what,
            message.screen_x, message.screen_y, cursor_x, cursor_y);
    g_failures++;
  }
}

int main(void) {
  SDL_Window *window;

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SKIP: no SDL video here (%s)\n", SDL_GetError());
    return 77;
  }
  window = SDL_CreateWindow("x2 win32 pointer test", 640, 480, 0);
  if (!window) {
    fprintf(stderr, "SKIP: no SDL window here (%s)\n", SDL_GetError());
    SDL_Quit();
    return 77;
  }
  x2_settings_store()->width = 1280;
  x2_settings_store()->height = 720;
  x2_win32_pointer_window(window);

  cursor_agrees(window, 320.0f, 240.0f,
                "a warp to the window centre moved the cursor");
  cursor_agrees(window, 17.0f, 401.0f, "a warp off centre moved the cursor");

  SDL_DestroyWindow(window);
  SDL_Quit();
  if (g_failures) {
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  printf("win32_pointer: GetCursorPos agrees with WM_MOUSEMOVE\n");
  return 0;
}
