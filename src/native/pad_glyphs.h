#pragma once

#include <cstdint>

namespace x2::native {

/* XMen2.exe FUN_006281f0's physical-input code -> published font byte.
   Returns zero for codes whose original text name must remain in use. */
uint8_t pad_glyph_code(uint32_t code);

void pad_glyphs_report(void);

} // namespace x2::native
