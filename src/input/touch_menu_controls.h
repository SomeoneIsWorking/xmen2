#ifndef X2_TOUCH_MENU_CONTROLS_H
#define X2_TOUCH_MENU_CONTROLS_H

#include "touch_controls.h"

#include <span>
#include <vector>

#include <lucent/touch.h>

namespace x2::input {

/*
 * The menu pad: the controller every screen that is not gameplay is driven
 * with in touch play (x2_layout_build_menu places it).
 *
 * The retail menus were built for a controller and name its buttons in their
 * footers, so the port offers that controller rather than guessing which
 * drawn text a finger meant. A contact that begins on none of its buttons is
 * not this owner's: the caller hands it to the retail pointer instead.
 */
class MenuControls {
public:
  // Returns releases for contacts captured under the old layout, to publish
  // before the new one applies.
  std::vector<ActionEvent> set_viewport(Viewport viewport);
  // Is this point on one of the pad's buttons?
  bool hit(lucent::touch::Point point) const;
  std::vector<ActionEvent>
  route(std::span<const lucent::touch::Contact> contacts);
  std::vector<ActionEvent> cancel();
  std::span<const TouchControls::ZoneVisual> zones() const { return zones_; }

private:
  std::vector<ActionEvent>
  translate(std::span<const lucent::touch::Event> events) const;

  std::vector<TouchControls::ZoneVisual> zones_;
  lucent::touch::Router router_;
};

} // namespace x2::input

#endif /* X2_TOUCH_MENU_CONTROLS_H */
