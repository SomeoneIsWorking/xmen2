/* Prompt-glyph insertion at Alchemy's finalized non-indexed draw boundary. */
#pragma once

#include "x86rt.h"

namespace x2::native {

void prompt_glyph_batch_draw_nonindexed(CPU *C);
void prompt_glyph_batch_update_context_state(CPU *C);
void prompt_glyph_batch_report(void);

} // namespace x2::native
