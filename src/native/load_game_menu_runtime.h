#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace x2::native {

/* Load Game projects more saves than a popup page holds; while it does, the
   popup's list is every save, of which the page shows `rows` from `first`. */
struct LoadGameMenuList {
  std::size_t entries = 0;
  std::size_t first = 0;
};
std::optional<LoadGameMenuList> load_game_menu_list(std::uint32_t popup);

/* Shows `rows` saves from `first` on the page; the page's first shown entry
   stays 0. */
void load_game_menu_show(std::size_t first, std::size_t rows);

std::size_t load_game_menu_runtime_report(char *out, std::size_t capacity);

} // namespace x2::native
