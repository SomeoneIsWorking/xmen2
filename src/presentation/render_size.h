#pragma once

#include "settings.h"

struct SDL_Window;

namespace x2::presentation {

/*
 * The one rule for the size the game renders at (settings width x height),
 * from the player's choice (settings render_lines):
 *
 * - Native, where the output surface is the whole display (Android always,
 *   desktop borderless and fullscreen): the surface's own pixel size -- the
 *   live window's when it exists, the display's before it does.
 * - Native in a desktop window: the window's size, which is the stored
 *   width x height; the window is created and kept at it.
 * - A preset: that many lines, the width in the screen's shape.
 */

/* Whether the output surface is the whole display for `mode`. */
bool output_is_display(x2::config::WindowMode mode);

/* Resolve settings width/height for `window` (null before it exists).
   Returns 1 when they changed. */
int render_size_resolve(x2::config::Settings *settings, SDL_Window *window);

/* The size to create the host window at: the display where the output is
   the display, else the render size. */
void render_size_window(const x2::config::Settings *settings, unsigned *width,
                        unsigned *height);

} // namespace x2::presentation
