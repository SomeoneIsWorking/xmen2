#include "list_touch_layout.hpp"
#include "native_touch_menus.hpp"

#include <algorithm>

namespace x2::input {
namespace {

/* Text and icon inset from a card's left edge, and an icon's size, in card
   heights. */
constexpr float kTextPad = 0.3F;
constexpr float kIconSize = 0.8F;

} // namespace

ListTouchLayout layout_list_touch(const ListTouchInput &input) {
  ListTouchLayout out;
  if (!(input.line > 0.0F) || !(input.finger > 0.0F) ||
      !(input.box.height() > 0.0F)) {
    return out;
  }
  out.cards = stack_cards(input.box, input.finger, input.entries, input.top,
                          input.max_rows);
  out.text_scale = kCardText * input.finger / input.line;
  const float card = kCardHeight * input.finger;
  out.icon = input.icons ? kIconSize * card : 0.0F;
  out.pad = kTextPad * card;
  out.icon_inset =
      std::max(out.pad, input.art_frame * input.box.width() + 0.5F * out.pad);
  out.text_inset = out.icon_inset + (input.icons ? out.icon + out.pad : 0.0F);
  return out;
}

std::optional<SceneBox> nested_list_area(const ListPlace &self,
                                         const std::vector<ListPlace> &others) {
  const auto inside = [](const SceneBox &inner, const SceneBox &outer) {
    return outer.left <= inner.left && inner.right <= outer.right &&
           outer.bottom <= inner.bottom && inner.top <= outer.top;
  };
  const auto size = [](const SceneBox &box) {
    return box.width() * box.height();
  };
  SceneBox outer = self.box;
  bool other_focused = false;
  bool nested = false;
  for (const ListPlace &other : others) {
    if (!inside(other.box, self.box) && !inside(self.box, other.box)) {
      continue;
    }
    nested = true;
    other_focused = other_focused || other.focused;
    if (size(other.box) > size(outer)) {
      outer = other.box;
    }
  }
  if (!nested) {
    return self.box;
  }
  const bool shows =
      self.focused || (!other_focused && size(outer) == size(self.box));
  return shows ? std::optional<SceneBox>(outer) : std::nullopt;
}

bool list_has_touch_layout(menu::ItemClass item_class,
                           std::string_view menu_class) {
  return (item_class == menu::ItemClass::list_box ||
          item_class == menu::ItemClass::list_codex) &&
         menu_is_native_on_touch(menu_class);
}

} // namespace x2::input
