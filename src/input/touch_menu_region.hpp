#ifndef X2_TOUCH_MENU_REGION_HPP
#define X2_TOUCH_MENU_REGION_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuRegion: the online region list, one row per region with its count,
   and the Back, Refresh and Select footers. A tap on a region walks the
   list's selection to it with the menu pad; a tap on the selected one, or
   Select, presses A, which opens the campaign lobby (0x005d02d0). */
std::optional<TouchMenuView>
build_region_view(const menu::MenuSnapshot &menu,
                  const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
