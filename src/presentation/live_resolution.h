#ifndef X2_LIVE_RESOLUTION_H
#define X2_LIVE_RESOLUTION_H

#include "settings.h"

struct SDL_Window;

/*
 * Advance the configured resolution to the next height preset -- 720p, 1080p,
 * 1440p, 2160p -- and derive its width from the display's aspect ratio. See
 * resolution_ladder.h for the policy and display_geometry.h for the query.
 */
void x2_live_resolution_select_next(X2Settings *settings);

/*
 * Apply and persist one Port Settings resolution change as a transaction.
 * The caller has already placed the requested dimensions in `settings` and
 * supplies the complete prior value for rollback. On any failure, settings,
 * the SDL window policy, retained title display state, and active D3D8
 * presentation are restored.
 */
int x2_live_resolution_apply(struct SDL_Window *window, X2Settings *settings,
                             const X2Settings *before, char *why, int whyn);

#endif /* X2_LIVE_RESOLUTION_H */
