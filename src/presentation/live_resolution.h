#pragma once

#include "settings.h"

struct SDL_Window;

namespace x2::presentation {

/*
 * Step the render-resolution choice -- Native, 720p, 1080p, 1440p, 2160p --
 * forward (`direction` > 0) or back, and resolve the size it means for
 * `window` (render_size.h).
 */
void live_resolution_select(x2::config::Settings *settings,
                            struct SDL_Window *window, int direction);

/*
 * The output surface may have changed size (rotation, resize, the window
 * created at another size than predicted). Once a D3D8 device exists,
 * re-resolve the render size and apply it live when it differs; until then
 * the note is kept. Called from the event pump, between frames.
 */
void live_resolution_note_surface();

/* The size the game renders at now: the live backbuffer once a device
   exists, else the resolved settings. */
void live_resolution_rendered(const x2::config::Settings *settings,
                              unsigned *width, unsigned *height);
void live_resolution_follow_surface(struct SDL_Window *window);

/*
 * Apply and persist one Port Settings resolution change as a transaction.
 * The caller has already placed the requested dimensions in `settings` and
 * supplies the complete prior value for rollback. On any failure, settings,
 * the SDL window policy, retained title display state, and active D3D8
 * presentation are restored.
 */
int live_resolution_apply(struct SDL_Window *window,
                          x2::config::Settings *settings,
                          const x2::config::Settings *before, char *why,
                          int whyn);

} // namespace x2::presentation
