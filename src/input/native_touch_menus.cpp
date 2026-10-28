#include "native_touch_menus.hpp"

#include <algorithm>
#include <array>

namespace x2::input {
namespace {

constexpr std::array<std::string_view, 11> kNativeClasses = {
    "CMenuMain",   "CMenuOptions",     "CMenuPDA",        "CMenuShop",
    "CMenuCodex",  "CMenuReviewPaths", "CMenuDangerRoom", "CMenuTrivia",
    "CMenuRegion", "CMenuWorldMap",    "CMenuTeam"};

} // namespace

bool menu_is_native_on_touch(std::string_view menu_class) {
  return std::find(kNativeClasses.begin(), kNativeClasses.end(), menu_class) !=
         kNativeClasses.end();
}

} // namespace x2::input
