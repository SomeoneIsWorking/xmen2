#ifndef X2_TESTS_TOUCH_MENU_FIXTURE_HPP
#define X2_TESTS_TOUCH_MENU_FIXTURE_HPP

/* What the touch menu tests share: the check counter, a 1280x720 scene plane
   and viewport, a retail item and a tap. */
#include "../src/input/touch_menu.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace x2::test::touch_menu {

inline int failures;
inline int checks;

inline void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

using x2::input::TouchAction;
using x2::input::TouchMenu;
using x2::input::TouchMenuButton;
using x2::input::TouchMenuDelivery;
using x2::input::TouchMenuLayout;
using x2::input::TouchMenuPart;
using x2::input::TouchMenuView;
using x2::menu::MenuItem;
using x2::menu::MenuSnapshot;
using x2::presentation::RetailScenePlane;

inline constexpr float kAspect = 1280.0F / 720.0F;

inline RetailScenePlane plane_1280x720() {
  return *RetailScenePlane::from_viewport(kAspect, 1.0F, 1.0F, 1280u, 720u);
}

inline MenuItem item(unsigned slot, const char *name, const char *label,
                     int left, int top, int right, int bottom) {
  MenuItem out;
  out.slot = slot;
  out.name = name;
  out.label = label;
  out.rect = {left, top, right, bottom};
  out.flags = x2::menu::kItemEnabled;
  out.navigable = true;
  return out;
}

inline X2LayoutViewport viewport_1280x720() {
  X2LayoutViewport viewport{};
  viewport.width = 1280.0F;
  viewport.height = 720.0F;
  return viewport;
}

inline const TouchMenuButton *find(const TouchMenuLayout &layout,
                                   TouchMenuPart part, int index) {
  for (const TouchMenuButton &button : layout.buttons) {
    if (button.part == part && button.index == index) {
      return &button;
    }
  }
  return nullptr;
}

inline std::vector<TouchMenuDelivery> tap(TouchMenu &menu, const X2Rect &rect,
                                          std::uint64_t now) {
  const lucent::touch::Point at{0.5F * (rect.left + rect.right),
                                0.5F * (rect.top + rect.bottom)};
  menu.contact(1, at, lucent::touch::Phase::began, now);
  return menu.contact(1, at, lucent::touch::Phase::ended, now);
}

inline int report(const char *suite) {
  if (failures) {
    std::printf("%s: %d of %d check(s) failed\n", suite, failures, checks);
    return 1;
  }
  std::printf("%s: %d check(s) passed\n", suite, checks);
  return 0;
}

} // namespace x2::test::touch_menu

#endif
