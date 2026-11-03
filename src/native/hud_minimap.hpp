#ifndef X2_HUD_MINIMAP_HPP
#define X2_HUD_MINIMAP_HPP

#include "../presentation/touch_layout.h"

/* The round minimap (CHudAutoMap mode 1) moved and shrunk into the mobile
   layout's minimap square while that layout is in use (docs/RE/hud.md). */
namespace x2::native {

/* The square the minimap is drawn in while it is moved, in output pixels.
   Any thread. */
bool hud_minimap_placed(x2::presentation::Rect *out);

} // namespace x2::native

#endif
