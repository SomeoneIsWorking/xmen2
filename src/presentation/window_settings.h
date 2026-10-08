#pragma once

#include "settings.h"

struct SDL_Window;

namespace x2::presentation {

/* Apply the user's presentation policy to the one host window. A successful
   call is synchronous, so the renderer never observes a half-switched mode. */
int window_settings_apply(SDL_Window *window,
                          const x2::config::Settings *settings, char *why,
                          int whyn);

/* Guest Win32 size/move calls describe the original 2005 window. Once the
   host settings policy owns presentation they must not overwrite it. */
int window_settings_owns_geometry();

} // namespace x2::presentation
