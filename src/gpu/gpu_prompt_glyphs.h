/* Native textured 2D owner for the port's SVG prompt-glyph atlas. */
#ifndef GPU_PROMPT_GLYPHS_H
#define GPU_PROMPT_GLYPHS_H

/* Called before engine drawing starts, while GPU resources may be uploaded. */
void gpu_prompt_glyphs_frame_begin(void);

/* Submit queued engine text-plane quads through the owning batch matrix. */
int gpu_prompt_glyphs_render(const float mvp[16]);

void gpu_prompt_glyphs_shutdown(void);
void gpu_prompt_glyphs_report(void);
int gpu_prompt_glyphs_selftest(void);

#endif
