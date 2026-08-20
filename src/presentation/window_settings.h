#ifndef X2_WINDOW_SETTINGS_H
#define X2_WINDOW_SETTINGS_H

#include "settings.h"

struct SDL_Window;

/* Apply the user's presentation policy to the one host window. A successful
   call is synchronous, so the renderer never observes a half-switched mode. */
int x2_window_settings_apply(struct SDL_Window *window,
                             const X2Settings *settings,
                             char *why, int whyn);

/* Guest Win32 size/move calls describe the original 2005 window. Once the
   host settings policy owns presentation they must not overwrite it. */
int x2_window_settings_owns_geometry(void);

#endif
