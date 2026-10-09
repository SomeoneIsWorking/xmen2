/* Native textured 2D owner for the port's SVG prompt-glyph atlas. */
#pragma once

#include "../native/prompt_glyph_quads.h"

namespace x2::gpu {

/* Called before engine drawing starts, while GPU resources may be uploaded. */
void gpu_prompt_glyphs_frame_begin(void);

/* Submit one draw's engine text-plane quads through its batch matrix. */
int gpu_prompt_glyphs_render(const struct x2::native::PromptQuad *quads,
                             unsigned count, const float mvp[16]);

void gpu_prompt_glyphs_shutdown(void);
void gpu_prompt_glyphs_report(void);
int gpu_prompt_glyphs_selftest(void);

} // namespace x2::gpu
