/* Prompt-glyph insertion at Alchemy's finalized non-indexed draw boundary. */
#ifndef X2_PROMPT_GLYPH_BATCH_H
#define X2_PROMPT_GLYPH_BATCH_H

#include "x86rt.h"

void x2_prompt_glyph_batch_draw_nonindexed(CPU *C);
void x2_prompt_glyph_batch_update_context_state(CPU *C);
void x2_prompt_glyph_batch_report(void);

#endif
