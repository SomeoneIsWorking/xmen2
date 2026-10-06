#ifndef X2_RESOLUTION_LADDER_H
#define X2_RESOLUTION_LADDER_H

#include <stddef.h>

/*
 * The resolution setting is a HEIGHT: 720p, 1080p, 1440p, 2160p. Width is
 * derived from the display's own aspect ratio rather than chosen from a list,
 * so a 16:10 or 21:9 panel gets a mode that fills it instead of the nearest
 * 16:9 entry pillarboxed inside it.
 *
 * Pure policy: the display size is a parameter, not a query. Pass 0/0 when the
 * display is unknown and the ladder assumes 16:9.
 */

/* Width in pixels for `height` on a `display_w` x `display_h` display, rounded
   to an even number of pixels. */
unsigned x2_resolution_width_for(unsigned height, unsigned display_w,
                                 unsigned display_h);

/* The next height after `height`, wrapping, skipping presets taller than the
   display. An unrecognised `height` -- a legacy stored 900 or 768, or a first
   run -- selects the first preset. 720p is always offered even on a shorter
   display, because refusing to offer any resolution is worse than offering one
   the display must scale. */
unsigned x2_resolution_next_height(unsigned height, unsigned display_h);

/* "1080p" for 1080. Always NUL-terminates; returns the number of characters
   written, or 0 when `buf` cannot hold the label. */
size_t x2_resolution_label(unsigned height, char *buf, size_t size);

#endif /* X2_RESOLUTION_LADDER_H */
