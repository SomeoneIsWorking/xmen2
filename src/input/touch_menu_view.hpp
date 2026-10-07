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
  /* A row reached by a focus walk is pressed with A on arrival; a list entry
     is only selected, as a click on it would. */
  bool press_on_arrival = true;
  /* The retail item, as its slot; stable while the menu is up. */
  unsigned slot = 0;
  /* The entry within a list box item, or -1 for the item itself. */
  int entry = -1;
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

/* A tab of a class with tabs: a click on its own item opens it. */
struct TouchMenuTab {
  std::string label;
  /* The game's open tab. */
  bool lit = false;
  presentation::ClientPoint click;
};

/* A value the game shows beside its list, as its own label and text. */
struct TouchMenuFact {
  std::string label;
  std::string value;
  /* The game draws the value in its warning style. */
  bool warn = false;
};

struct TouchMenuView {
  std::uint32_t address = 0;
  std::string menu;
  std::string menu_class;
  std::string title;
  std::vector<TouchMenuRow> rows;
  std::vector<TouchMenuFooter> footers;
  /* Pinned above the rows; they do not scroll. */
  std::vector<TouchMenuTab> tabs;
  /* Pinned below the rows: the game's text about the focused row, in the
     game's own lines. A view with no rows is read: its lines fill the list's
     place and scroll there. */
  std::vector<TouchMenuFact> facts;
  std::vector<std::string> detail;
  /* Index into `rows` of the menu's own focus, or -1. */
  int focused_row = -1;
  /* The game's Up/Down wraps from the last row to the first. */
  bool focus_wraps = true;
  /* When set, the game's Up/Down turns a wrapping cycle of this many
     positions and the rows show some of them, each at its `entry`; a walk
     steps by position, not by row. */
  int cycle = 0;
  /* The cycle position the game has focused, shown as a row or not. */
  int cycle_focus = -1;

  /* The same retail screen: menu object, name and row set. */
  bool same_screen(const TouchMenuView &other) const;
};

/* The view of a menu the touch menu replaces, or nullopt: another class, a
   popup over it, a screen of the class it does not cover, or no selectable
   row. Each replaced class has its own builder. */
std::optional<TouchMenuView>
build_touch_menu_view(const menu::MenuSnapshot &menu,
                      const presentation::RetailScenePlane &plane);

/* Retail text as a player reads it: style escapes ("~05") and prompt tokens
   ("$MENU_BACK") removed, whitespace collapsed, and letter-spaced titles
   ("r i s e   o f") closed up. */
std::string touch_menu_text(std::string_view raw);

} // namespace x2::input

#endif
