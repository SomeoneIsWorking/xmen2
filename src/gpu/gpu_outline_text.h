/* Outline text at its drawn pixel size: picks each glyph's raster from the
 * text batch's matrix and the viewport, and keeps the cache's atlas pages on
 * the GPU. See docs/RE/text.md. */
#pragma once

#include "../native/prompt_glyph_quads.h"
#include "gpu_draw.h"

namespace x2::gpu {

/* A new frame: the glyph cache may start again empty. */
void gpu_outline_text_frame_begin();

/* The textured quad that draws outline glyph `glyph` (a pen and cap height,
   see PromptQuad) one texel per target pixel under `mvp` and the current
   viewport, and the page it samples; false when the glyph has no ink, is too
   small to see, or found no room. */
bool gpu_outline_text_place(const x2::native::PromptQuad &glyph,
                            const float mvp[16], x2::native::PromptQuad *quad,
                            unsigned *page);

/* Upload what placing wrote; false when a page could not reach the GPU. */
bool gpu_outline_text_upload();

/* Page `page`'s texture; 0 before it was uploaded. */
GpuTexture gpu_outline_text_page(unsigned page);

void gpu_outline_text_shutdown();
void gpu_outline_text_report();

} // namespace x2::gpu
