#pragma once

#include "retail_menu_model.hpp"

#include <cstdint>
#include <optional>

namespace x2::native {

/* The world map's footer after its init (0x005e8c40) shows Back twice,
   desctext1 and the XMLB's desctext2; docs/RE/menus.md, "The two Backs are the
   game's own". The address of the shown desctext2 when it is a Back prompt
   beside another one; nullopt otherwise. */
std::optional<std::uint32_t>
secondary_back_footer(const x2::menu::MenuSnapshot &menu);

} // namespace x2::native
