#pragma once

#include "../presentation/touch_layout.h"
#include "x86rt.h"

namespace x2::native {

void hud_party_draw(CPU *cpu);
void hud_draw_report();

/* The band of the HUD lines drawn apart from the use prompt's button, in
   output pixels, while it is drawn. */
bool hud_message_band(x2::presentation::Rect *out);

/* Where the mobile layout puts the round minimap: its centre and radius in
   HUD units (z up), and its square in output pixels. */
struct HudDisc {
  float x;
  float z;
  float radius;
  x2::presentation::Rect box;
};
/* False unless the last HUD frame used the mobile layout. Guest thread. */
bool hud_minimap_target(HudDisc *out);

} // namespace x2::native
