#ifndef X2_AUTOMAP_TOUCH_LAYOUT_HPP
#define X2_AUTOMAP_TOUCH_LAYOUT_HPP

#include "../presentation/retail_scene_plane.hpp"

/* The automap on touch (docs/touch-play.md): a drag moves the map with the
   finger. World units on the map's ground plane; scene units z up. */
namespace x2::input {

struct MapPoint {
  float x = 0.0F;
  float y = 0.0F;
};

/* Where the map looks (CMenuAutoMap +0x18ec), its turn (+0x18f8) and the
   bounds it is held in (+0x18dc..+0x18e8). */
struct MapView {
  MapPoint at;
  float turn = 0.0F;
  MapPoint low;
  MapPoint high;
};

/* Where the map looks once a finger that pressed while it looked at
   `start` has moved `moved` scene units: the ground under the finger stays
   under it. The map shows `scale` scene units per world unit, turned by
   `start.turn` (the markers' draw, FUN_005a0bb0), and is held in its bounds
   as the update holds it (FUN_005afbd0). */
MapPoint automap_dragged(const MapView &start, presentation::ScenePoint moved,
                         float scale);

} // namespace x2::input

#endif
