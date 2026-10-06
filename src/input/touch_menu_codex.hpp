#ifndef X2_TOUCH_MENU_CODEX_HPP
#define X2_TOUCH_MENU_CODEX_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuCodex (mode at menu+0x18d8): in mode 0 one row per entry of its list,
   a tap selecting and accepting, which loads the entry; in mode 1, which
   Details opens, the loaded entry's name and description in the game's own
   lines, scrolled by drag. Its footers are the game's. */
std::optional<TouchMenuView>
build_codex_view(const menu::MenuSnapshot &menu,
                 const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
