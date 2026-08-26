/* Harvested prompt-glyph rectangles. See prompt_glyph_draw.c. */
#ifndef X2_PROMPT_GLYPH_QUADS_H
#define X2_PROMPT_GLYPH_QUADS_H

#include <stdint.h>

/* One prompt glyph, positioned by the ENGINE's own layout, in the engine's
   text space -- the coordinates FUN_005ee400 receives. u0..v1 are the port
   atlas's UVs for the codepoint, not the game font's. */
struct X2PromptQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    uint16_t codepoint;
};

/* The frame's harvest. Valid until the next x2_prompt_quads_reset(). */
const struct X2PromptQuad *x2_prompt_quads(unsigned *count);
void x2_prompt_quads_reset(void);
void x2_prompt_quads_add(const struct X2PromptQuad *quad);
void x2_prompt_quads_report(void);

#endif
