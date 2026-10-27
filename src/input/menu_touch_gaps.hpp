#ifndef X2_MENU_TOUCH_GAPS_HPP
#define X2_MENU_TOUCH_GAPS_HPP

#include "../native/retail_menu_model.hpp"
#include "../presentation/retail_scene_plane.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

/* What a finger cannot do on a retail menu through the game's own pointer
   path (docs/RE/menus.md), named by the guest function that refuses it. */
namespace x2::input {

enum class MenuTouchGapKind : std::uint8_t {
  /* A row with no usecmd: CMenuItem::onMouse (0x005bc1b0) ignores it. */
  row_without_command,
  /* leftcmd/rightcmd: CMenu::onMouse (0x005ae0a0) has no left or right. */
  step,
  /* A list box: its onMouse (0x005c0e10) selects but never scrolls. */
  list,
  /* A footer token CMenuItemText::onMouse (0x005c6490) does not publish. */
  footer_token,
  /* A hit box narrower or shorter than a finger. */
  small_target,
  /* A popup takes the pointer first (popup+0x7c); not mapped yet. */
  popup,
};

const char *menu_touch_gap_kind_name(MenuTouchGapKind kind);

struct MenuTouchGap {
  MenuTouchGapKind kind = MenuTouchGapKind::row_without_command;
  /* The item's XMLB name; the token for footer_token. */
  std::string item;
};

/* A finger's side in client pixels: a tenth of the shorter client edge, a
   fingertip on a phone held sideways. */
float menu_finger_target(const presentation::RetailScenePlane &plane);

std::vector<MenuTouchGap>
find_menu_touch_gaps(const menu::MenuSnapshot &menu,
                     const presentation::RetailScenePlane &plane);

/* The gaps of every menu seen this run, counted once per opening. */
class MenuTouchGapReport {
public:
  void observe(const menu::MenuSnapshot &menu,
               const presentation::RetailScenePlane &plane);
  /* One line per gap through the project logger. */
  void report() const;

private:
  using Key = std::tuple<std::string, MenuTouchGapKind, std::string>;
  std::map<Key, unsigned> counts_;
  std::uint32_t open_menu_ = 0u;
  std::vector<Key> open_gaps_;
};

} // namespace x2::input

#endif
