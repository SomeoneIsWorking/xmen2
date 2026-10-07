#include "world_map_footer.hpp"

#include <cstdio>
#include <string>

namespace {

int failures;

void check(bool ok, const char *what) {
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

x2::menu::MenuItem footer(const char *name, const char *label,
                          std::uint32_t address, std::uint8_t flags = 0x08u) {
  x2::menu::MenuItem item;
  item.name = name;
  item.label = label;
  item.address = address;
  item.flags = flags;
  return item;
}

/* The world map as read live after init: Back, back, go. */
x2::menu::MenuSnapshot world_map() {
  x2::menu::MenuSnapshot menu;
  menu.menu_class = "CMenuWorldMap";
  menu.items.push_back(footer("desctext1", "~05$MENU_BACK Back", 0x1000u));
  menu.items.push_back(footer("desctext2", "~05$MENU_BACK back", 0x2000u));
  menu.items.push_back(footer("desctext4", "~05$MENU_ACCEPT go", 0x4000u));
  return menu;
}

} // namespace

int main() {
  const auto hidden = x2::native::secondary_back_footer(world_map());
  check(hidden && *hidden == 0x2000u, "hides desctext2, not desctext1/4");

  x2::menu::MenuSnapshot other = world_map();
  other.menu_class = "CMenuCodex";
  check(!x2::native::secondary_back_footer(other), "only the world map");

  x2::menu::MenuSnapshot shown_once = world_map();
  shown_once.items[0].label = "";
  check(!x2::native::secondary_back_footer(shown_once),
        "never hides the only Back");

  x2::menu::MenuSnapshot not_back = world_map();
  not_back.items[1].label = "$MENU_ACCEPT go";
  check(!x2::native::secondary_back_footer(not_back),
        "desctext2 must be a Back prompt");

  x2::menu::MenuSnapshot done = world_map();
  done.items[1].flags |= x2::menu::kItemHidden;
  check(!x2::native::secondary_back_footer(done), "already hidden");

  if (failures != 0) {
    return 1;
  }
  std::printf("world_map_footer: ok\n");
  return 0;
}
