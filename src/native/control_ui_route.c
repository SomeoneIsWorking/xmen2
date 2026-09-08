/*
 * The control channel's two HOST-layer input routes.
 *
 * /key and /pad drive the GAME: they are injected into its own DirectInput
 * poll. Nothing there can reach the port's own overlay, which is an SDL
 * document with SDL input -- so a run could be driven from outside the process
 * in every respect except the one part the port itself owns. These two routes
 * close that gap, and they are kept apart from the guest-facing routes so the
 * difference is visible in the file a reader opens.
 */
#include "control_ui_route.h"

#include "control_http.h"
#include "control_query.h"
#include "gpu_device.h"

#include <SDL3/SDL.h>

#include <stdlib.h>

/*
 * Press a key at the HOST window layer, not at the guest's keyboard.
 *
 * /key injects into the game's own DirectInput poll, which is the right thing
 * for anything the game reads -- and the wrong thing for the port's own
 * hotkeys. F2 opens Port Settings from an SDL key event, so a DirectInput
 * injection of F2 is delivered to a game that has no binding for it and the
 * overlay never opens. Without this route the port's own UI is the one part of
 * the run that cannot be driven from outside it, which is exactly where a
 * change like a live resolution switch has to be exercised.
 *
 * SDL_PushEvent is the documented cross-thread way in, and the event lands in
 * the same queue the window's own presses do: the overlay consumes what it
 * owns, and anything it does not consume reaches the game as a real press.
 */
void control_ui_key_route(x2_socket_t fd, const char *query) {
  char name[32] = "";
  SDL_Keycode key;
  SDL_Event event;
  Uint64 now;

  if (!control_query_arg(query, "name", name, sizeof name) || !name[0]) {
    control_reply_text(fd, 400, "Bad Request",
                       "no key named. Use /ui/key?name=F2.\n"
                       "Names are SDL key names: F2, Tab, Return, Escape,\n"
                       "Up, Down, Left, Right, Space ...\n");
    return;
  }
  key = SDL_GetKeyFromName(name);
  if (key == SDLK_UNKNOWN) {
    control_reply_text(fd, 400, "Bad Request",
                       "SDL does not know a key named \"%s\".\n", name);
    return;
  }
  now = SDL_GetTicksNS();
  SDL_zero(event);
  event.key.type = SDL_EVENT_KEY_DOWN;
  event.key.timestamp = now;
  event.key.key = key;
  event.key.scancode = SDL_GetScancodeFromKey(key, &event.key.mod);
  event.key.down = true;
  if (!SDL_PushEvent(&event)) {
    control_reply_text(fd, 503, "Service Unavailable",
                       "SDL refused the press: %s\n", SDL_GetError());
    return;
  }
  event.key.type = SDL_EVENT_KEY_UP;
  event.key.timestamp = now + 1u;
  event.key.down = false;
  if (!SDL_PushEvent(&event)) {
    control_reply_text(fd, 503, "Service Unavailable",
                       "the press was delivered but SDL refused the "
                       "release: %s\n",
                       SDL_GetError());
    return;
  }
  control_reply_text(fd, 200, "OK",
                     "pushed host \"%s\" (down and up) at frame %lu\n", name,
                     gpu_frames_presented());
}

/*
 * Click at the HOST window layer, for the port's own overlay.
 *
 * Port Settings is laid out as a document and its rows are reached with the
 * pointer; nothing in it declares a tab-index, so a key press alone cannot
 * move between the tabs or the rows. Driving it from outside the process
 * therefore needs the pointer as well as the keyboard.
 */
void control_ui_click_route(x2_socket_t fd, const char *query) {
  char xs[16] = "", ys[16] = "";
  SDL_Event event;
  Uint64 now;
  float x, y;

  if (!control_query_arg(query, "x", xs, sizeof xs) ||
      !control_query_arg(query, "y", ys, sizeof ys)) {
    control_reply_text(fd, 400, "Bad Request",
                       "no point given. Use /ui/click?x=279&y=97.\n"
                       "Coordinates are window pixels, the same frame a "
                       "/screenshot is in.\n");
    return;
  }
  x = (float)atof(xs);
  y = (float)atof(ys);
  now = SDL_GetTicksNS();
  SDL_zero(event);
  event.motion.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.timestamp = now;
  event.motion.which = SDL_TOUCH_MOUSEID + 1u; /* not the touch synthetic */
  event.motion.x = x;
  event.motion.y = y;
  if (!SDL_PushEvent(&event)) {
    control_reply_text(fd, 503, "Service Unavailable",
                       "SDL refused the pointer move: %s\n", SDL_GetError());
    return;
  }
  SDL_zero(event);
  event.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.timestamp = now + 1u;
  event.button.which = SDL_TOUCH_MOUSEID + 1u;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.down = true;
  event.button.clicks = 1;
  event.button.x = x;
  event.button.y = y;
  if (!SDL_PushEvent(&event)) {
    control_reply_text(fd, 503, "Service Unavailable",
                       "SDL refused the press: %s\n", SDL_GetError());
    return;
  }
  event.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.timestamp = now + 2u;
  event.button.down = false;
  if (!SDL_PushEvent(&event)) {
    control_reply_text(fd, 503, "Service Unavailable",
                       "the press was delivered but SDL refused the "
                       "release: %s\n",
                       SDL_GetError());
    return;
  }
  control_reply_text(fd, 200, "OK", "clicked host (%.0f, %.0f) at frame %lu\n",
                     (double)x, (double)y, gpu_frames_presented());
}
