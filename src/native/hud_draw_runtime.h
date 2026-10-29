#pragma once

#include "../presentation/touch_layout.h"
#include "x86rt.h"

namespace x2::native {

void hud_party_draw(CPU *cpu);
void hud_draw_report();

/* The band of the HUD lines drawn apart from the use prompt's button, in
   output pixels, while it is drawn. */
bool hud_message_band(x2::presentation::Rect *out);

} // namespace x2::native
