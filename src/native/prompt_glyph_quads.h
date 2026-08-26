/* Harvested prompt-glyph rectangles. See prompt_glyph_draw.c. */
#ifndef X2_PROMPT_GLYPH_QUADS_H
#define X2_PROMPT_GLYPH_QUADS_H

#include <stdint.h>

/* One prompt glyph in the ENGINE text plane. The stock sink stores each
   corner as (x, 0, y), and the owning text batch supplies the transform.
   u0..v1 are the port atlas's bottom-origin UVs; color is the D3DCOLOR
   captured from the engine's text batch. */
struct X2PromptQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    uint32_t color;
    uint16_t codepoint;
};

#define X2_PROMPT_QUADS_MAX 512u

/* The frame's harvest. Valid until the next x2_prompt_quads_reset(). */
const struct X2PromptQuad *x2_prompt_quads(unsigned *count);
void x2_prompt_quads_reset(void);
/* Clear a successfully submitted batch without starting a new frame. */
void x2_prompt_quads_consume(void);
/* Remaining complete-quad slots. A string producer checks this before it
   enters the retail loop so native interception is all-or-nothing. */
unsigned x2_prompt_quads_available(void);
/* 1 means the complete quad was retained. A producer must reserve an entire
   string before its first call; 0 after reservation is an invariant failure. */
int x2_prompt_quads_add(const struct X2PromptQuad *quad);
void x2_prompt_quads_report(void);


#endif
