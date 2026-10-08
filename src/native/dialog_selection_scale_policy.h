#pragma once

#include <cstdint>

namespace x2::native {

/* Exact retail formula observed at XMen2.exe FUN_005ea9e0. */
float dialog_selection_retail_scale(uint32_t output_height);

/* Extends that formula beyond its 800x600 UI reference without allowing the
   original linear approximation to cross zero. */
float dialog_selection_scale(uint32_t output_height);

/* FUN_005ea9e0 places the row at a vertical translation that includes
   7.0 * retail_scale (the 7.0 is the float at 0x00686030), so the row's
   position follows its scale. Replacing the scale without this correction
   leaves a row the right height in the wrong place: a full row low at 2160
   lines (#185). Returns what to add to that translation. */
float dialog_selection_offset_correction(uint32_t output_height);

} // namespace x2::native
