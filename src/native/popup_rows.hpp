#ifndef X2_POPUP_ROWS_HPP
#define X2_POPUP_ROWS_HPP

#include "../presentation/retail_scene_plane.hpp"

/* CPopupDialog's option rows on touch (docs/RE/menus.md): each option keeps
   its retail text line and owns a slot at least a finger tall. */
namespace x2::native {

/* The slot of an option whose text line is `line` scene units tall. */
float popup_row_slot(float line, const presentation::RetailScenePlane &plane);

} // namespace x2::native

#endif
