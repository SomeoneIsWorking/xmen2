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
#define X2_PROMPT_RUNS_MAX 128u

/*
 * The store is the frame's LAYOUT RECORD: one run per string the glyph loop
 * laid out, in layout order, holding the number of glyphs the retail emitter
 * produced for it and the quads of ours among them (none for ordinary text).
 * A text pass lays strings out before drawing them, and a non-indexed draw
 * submits a contiguous window of them -- one footer element, or a speaker's
 * name, line and response button together -- with its own world matrix. So a
 * draw takes the window its glyph count declares, and no other quads.
 */

/* The glyph count a non-indexed text draw of `primitives` submits, or 0 if
   it is not a run of whole glyphs. */
unsigned x2_prompt_draw_glyphs(uint32_t primitives);

/* Drop every run: a new frame. Prompt runs laid out and never drawn are
   counted. */
void x2_prompt_quads_reset(void);
/* Remaining quad slots; a producer checks this before it enters the retail
   loop so native interception is all-or-nothing. */
unsigned x2_prompt_quads_available(void);
/* Open the run of a string identified by `identity` whose emitter produces
   `emitted` glyphs. A pending run of the same string is a re-measurement and
   is replaced; a full record evicts its oldest run. Returns 0 only when a run
   is already open. */
int x2_prompt_quads_begin_run(uint32_t identity, unsigned emitted);
/* 1 means the quad was retained in the open run. A producer must reserve an
   entire string before its first call; 0 after reservation is an invariant
   failure. */
int x2_prompt_quads_add(const struct X2PromptQuad *quad);
void x2_prompt_quads_end_run(void);
/* Take the earliest contiguous window of runs that carries quads of ours and
   whose glyphs sum to `glyphs`: copy its quads into `out` (capacity
   X2_PROMPT_QUADS_MAX), drop the window, and return the quad count. 0 when
   no such window is pending. */
unsigned x2_prompt_quads_take_run(unsigned glyphs, struct X2PromptQuad *out);
/* Pending runs that carry quads of ours, and those quads. */
unsigned x2_prompt_quads_pending(unsigned *quads);
void x2_prompt_quads_report(void);

#endif
