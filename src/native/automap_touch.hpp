#ifndef X2_AUTOMAP_TOUCH_HPP
#define X2_AUTOMAP_TOUCH_HPP

#include "../input/automap_touch_layout.hpp"

#include <cstdint>

/* CMenuAutoMap on touch (docs/touch-play.md): a drag moves the map with the
   finger and the Rotate prompt's buttons turn it while held. */
namespace x2::native {

/* The turn a held Rotate button asks for this frame: -1 the camera's left,
   1 its right, 0 none. Set by the prompt's draw on the guest thread. */
void automap_touch_turn(int direction);

/* What CMenuAutoMap `menu` shows, and the map's scene units per world
   unit; for GET /menu. Guest thread. */
input::MapView automap_view(std::uint32_t menu);
float automap_scale();

} // namespace x2::native

#endif
