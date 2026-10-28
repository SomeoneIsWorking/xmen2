#ifndef X2_LIST_TOUCH_LAYOUT_HPP
#define X2_LIST_TOUCH_LAYOUT_HPP

#include "../native/retail_menu_model.hpp"
#include "touch_cards.hpp"

#include <optional>
#include <string_view>
#include <vector>

/* A CMenuItemListBox on touch: its entries as finger-tall cards over the
   list's own box, scrolled by a drag (docs/touch-play.md). Scene units, z
   up. */
namespace x2::input {

struct ListTouchInput {
  /* The list's box in the scene. */
  SceneBox box;
  /* One line of the list's font at scale 1, and a finger. */
  float line = 0.0F;
  float finger = 0.0F;
  int entries = 0;
  int top = 0;
  /* The list draws an icon before each entry. */
  bool icons = false;
  /* The card art's frame along its left edge, a share of its width. */
  float art_frame = 0.0F;
  int max_rows = 1;
};

struct ListTouchLayout {
  CardStack cards;
  float text_scale = 1.0F;
  /* An icon's size, the inset of icons and text from a card's right edge,
     where icons start and where text starts after a card's left edge. */
  float icon = 0.0F;
  float pad = 0.0F;
  float icon_inset = 0.0F;
  float text_inset = 0.0F;
};

ListTouchLayout layout_list_touch(const ListTouchInput &input);

/* A shown list of a menu: its box, and whether it has the menu's focus. */
struct ListPlace {
  SceneBox box;
  bool focused = false;
};

/* Where `self`'s cards go beside `others`, the other shown lists of its
   menu. Lists whose boxes nest (CMenuTeam's gear slots hold the pieces list
   inside their box) take turns in the outer box: the one with the menu's
   focus, or the outer one while none has it. nullopt while another
   shows. */
std::optional<SceneBox> nested_list_area(const ListPlace &self,
                                         const std::vector<ListPlace> &others);

/* The lists touch lays out: those whose rows are CMenuItemListBox's row draw
   (0x005c1d00), in a menu that is native on touch. */
bool list_has_touch_layout(menu::ItemClass item_class,
                           std::string_view menu_class);

} // namespace x2::input

#endif
