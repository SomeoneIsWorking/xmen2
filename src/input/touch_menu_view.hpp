#ifndef X2_TOUCH_MENU_VIEW_HPP
#define X2_TOUCH_MENU_VIEW_HPP

#include "../native/retail_menu_model.hpp"
#include "../presentation/retail_scene_plane.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace x2::input {

/* One selectable retail row as the touch menu offers it. */
struct TouchMenuRow {
  std::string label;
  /* The paired value item's text ("On", "Normal"); empty when there is none. */
  std::string value;
  /* A bar row's level, 0..1. */
  std::optional<float> fill;
  /* The row has a usecmd, so a click on its box accepts it
     (CMenuItem::onMouse, FUN_005bc1b0). */
  bool clicks = false;
  /* The row has leftcmd/rightcmd, which act on the focused row only. */
  bool steps = false;
  bool focused = false;
  /* The retail item, as its slot; stable while the menu is up. */
  unsigned slot = 0;
  /* The centre of the box the game hit-tests, in client coordinates. */
  presentation::ClientPoint click;
};

/* One footer action: a desctext item whose text carries a $MENU_ token, which
   CMenuItemText::onMouse (FUN_005c6490) publishes on a click. */
struct TouchMenuFooter {
  std::string token;
  std::string label;
  presentation::ClientPoint click;
};

struct TouchMenuView {
  std::uint32_t address = 0;
  std::string menu;
  std::string menu_class;
  std::string title;
  std::vector<TouchMenuRow> rows;
  std::vector<TouchMenuFooter> footers;
  /* Index into `rows` of the menu's own focus, or -1. */
  int focused_row = -1;

  /* The same retail screen: menu object, name and row set. */
  bool same_screen(const TouchMenuView &other) const;
};

/* The menu classes the touch menu replaces. Every other class keeps the retail
   screen and the menu pad. */
bool touch_menu_replaces(std::string_view menu_class);

/* The view of a menu the touch menu replaces, or nullopt: another class, a
   popup over it, or no selectable row. */
std::optional<TouchMenuView>
build_touch_menu_view(const menu::MenuSnapshot &menu,
                      const presentation::RetailScenePlane &plane);

/* Retail text as a player reads it: style escapes ("~05") and prompt tokens
   ("$MENU_BACK") removed, whitespace collapsed, and letter-spaced titles
   ("r i s e   o f") closed up. */
std::string touch_menu_text(std::string_view raw);

} // namespace x2::input

#endif
