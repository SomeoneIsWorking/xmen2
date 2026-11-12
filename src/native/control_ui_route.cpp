/*
 * The control channel's two HOST-layer input routes.
 *
 * /key and /pad are injected into the game's own DirectInput poll; these
 * push SDL events into the window's queue, which is what the port's own
 * readers (a key binding's capture, the pointer path) see.
 */
#include "control_ui_route.h"

#include "control_http.h"
#include "control_query.h"
#include "gpu_device.h"

#include <SDL3/SDL.h>

#include <stdlib.h>

#include <deque>
#include <string>

namespace x2::native {
namespace {

/* SDL keeps a pushed text event's pointer, not its text: what was typed
   lives as long as the channel. */
std::deque<std::string> g_typed;

} // namespace

/*
 * Press a key at the window: a down and an up pushed into SDL's queue, the
 * cross-thread way in, landing where the window's own presses do.
 */
void control_ui_key_route(x2::native::Socket fd, const char *query) {
  char name[32] = "";
  SDL_Keycode key;
  SDL_Event event;
  Uint64 now;

  if (!control_query_arg(query, "name", name, sizeof name) || !name[0]) {
    control_reply_text(fd, 400, "Bad Request",
                       "no key named. Use /ui/key?name=Return.\n"
                       "Names are SDL key names: Tab, Return, Escape,\n"
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
 * Click at the window: a pointer move, press and release in SDL's queue.
 */
void control_ui_click_route(x2::native::Socket fd, const char *query) {
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

/*
 * Type at the HOST window: one SDL text event, the one a keyboard or a
 * phone's screen keyboard sends. It reaches the game only while the
 * platform's text input is on.
 */
void control_ui_text_route(x2::native::Socket fd, const char *query) {
  char text[128] = "";
  SDL_Event event;

  if (!control_query_arg(query, "text", text, sizeof text) || !text[0]) {
    control_reply_text(fd, 400, "Bad Request",
                       "no text given. Use /ui/text?text=Logan.\n");
    return;
  }
  g_typed.emplace_back(text);
  SDL_zero(event);
  event.text.type = SDL_EVENT_TEXT_INPUT;
  event.text.timestamp = SDL_GetTicksNS();
  event.text.text = g_typed.back().c_str();
  if (!SDL_PushEvent(&event)) {
    control_reply_text(fd, 503, "Service Unavailable",
                       "SDL refused the text: %s\n", SDL_GetError());
    return;
  }
  control_reply_text(fd, 200, "OK", "typed \"%s\" at frame %lu\n", text,
                     gpu_frames_presented());
}

} // namespace x2::native
