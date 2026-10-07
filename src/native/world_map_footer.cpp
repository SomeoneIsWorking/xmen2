#include "world_map_footer.hpp"

#include <string_view>

namespace x2::native {
namespace {

constexpr std::string_view kBackToken = "$MENU_BACK";

bool is_back(const x2::menu::MenuItem &item) {
  /* The label keeps its style escape, "~05$MENU_BACK back". */
  return std::string_view(item.label).find(kBackToken) !=
         std::string_view::npos;
}

} // namespace

std::optional<std::uint32_t>
secondary_back_footer(const x2::menu::MenuSnapshot &menu) {
  if (menu.menu_class != "CMenuWorldMap") {
    return std::nullopt;
  }
  const x2::menu::MenuItem *primary = nullptr;
  const x2::menu::MenuItem *secondary = nullptr;
  for (const x2::menu::MenuItem &item : menu.items) {
    if (item.name == "desctext1") {
      primary = &item;
    } else if (item.name == "desctext2") {
      secondary = &item;
    }
  }
  if (primary == nullptr || secondary == nullptr || secondary->hidden() ||
      !is_back(*primary) || !is_back(*secondary)) {
    return std::nullopt;
  }
  return secondary->address;
}

} // namespace x2::native
