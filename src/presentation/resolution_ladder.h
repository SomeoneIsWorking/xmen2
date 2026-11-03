#pragma once

#include <cstddef>

namespace x2::presentation {

/*
 * The render-resolution choices: Native, then the height presets 720p, 1080p,
 * 1440p, 2160p. A preset's width is derived from the screen's own aspect
 * ratio rather than chosen from a list, so a 16:10 or 21:9 panel gets a mode
 * that fills it instead of the nearest 16:9 entry pillarboxed inside it.
 * Native (x2::config::kRenderNative, 0) has no height of its own; what it
 * resolves to is render_size.h's decision.
 *
 * Pure policy: the display size is a parameter, not a query. Pass 0/0 when the
 * display is unknown and the ladder assumes 16:9.
 */

/* Width in pixels for `height` on a `display_w` x `display_h` display, rounded
   to an even number of pixels. */
unsigned resolution_width_for(unsigned height, unsigned display_w,
                              unsigned display_h);

/* The choice after `lines` (`direction` > 0) or before it (< 0), wrapping
   through Native, skipping presets taller than the display. An unrecognised
   `lines` -- a legacy stored 900 or 768 -- selects Native. 720p is always
   offered even on a shorter display, because refusing to offer any
   resolution is worse than offering one the display must scale. */
unsigned resolution_step_lines(unsigned lines, unsigned display_h,
                               int direction);

/* "Native" for kRenderNative, "1080p" for 1080. Always NUL-terminates;
   returns the number of characters written, or 0 when `buf` cannot hold the
   label. */
size_t resolution_label(unsigned lines, char *buf, size_t size);

} // namespace x2::presentation
