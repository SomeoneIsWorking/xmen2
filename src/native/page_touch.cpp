/*
 * The retail pages with no list of their own on touch (docs/touch-play.md,
 * docs/RE/menus.md): CMenuImageViewer and CMenuCredits share the onMouse
 * 0x005bb590, which only runs the base CMenu::onMouse (their footers). A
 * finger that starts off every touch button also does what the pad would: a
 * swipe across the image viewer shows the next or previous screen, and a
 * tap on the credits ends them as A or B does.
 */
#include "../input/menu_touch_gaps.hpp"
#include "../input/page_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "../input/touch_targets.hpp"
#include "guest_body.h"
#include "guest_modules.h"
#include "menu_action.hpp"
#include "retail_menu_model.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace {

using x2::presentation::ScenePoint;

/* The shared onMouse(x, z, wparam, message, lparam), ret 0x14. */
constexpr std::uint32_t kOnMouse = 0x005bb590u;

constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

class PageTouch {
public:
  void pointer(std::uint32_t self, std::string_view menu_class,
               ScenePoint point, std::uint32_t message) {
    if (message == kButtonDown) {
      press_.reset();
      if (!x2::input::touch_targets().holding()) {
        press_ = Press{self, point};
      }
      return;
    }
    if (message != kButtonUp || !press_ || press_->menu != self) {
      return;
    }
    const auto plane = x2::native::live_scene_plane();
    const ScenePoint from = press_->at;
    press_.reset();
    if (!plane) {
      return;
    }
    act(menu_class, x2::input::page_gesture(
                        from, point, x2::input::menu_finger_units(*plane)));
  }

private:
  struct Press {
    std::uint32_t menu = 0u;
    ScenePoint at;
  };

  static void act(std::string_view menu_class, x2::input::PageGesture gesture) {
    using x2::input::PageGesture;
    using x2::native::MenuAction;
    if (menu_class == "CMenuImageViewer" &&
        gesture == PageGesture::swipe_next) {
      x2::native::post_menu_action(MenuAction::next);
    } else if (menu_class == "CMenuImageViewer" &&
               gesture == PageGesture::swipe_prev) {
      x2::native::post_menu_action(MenuAction::prev);
    } else if (menu_class == "CMenuCredits" && gesture == PageGesture::tap) {
      x2::native::post_menu_action(MenuAction::back);
    }
  }

  std::optional<Press> press_;
};

PageTouch g_page_touch;

} // namespace

extern "C" void x2_override_005bb590(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const std::uint32_t self = cpu->reg[kX86pEcx];
  if (on_touch()) {
    const auto x = static_cast<std::int32_t>(RD32(esp + 4u));
    const auto z = static_cast<std::int32_t>(RD32(esp + 8u));
    g_page_touch.pointer(
        self, x2::menu::classify_menu(RD32(self), x2::native::guest_exe_base()),
        {static_cast<float>(x), static_cast<float>(z)}, RD32(esp + 16u));
  }
  x86_guest_body(cpu, "XMen2.exe", kOnMouse);
}

__attribute__((constructor)) static void x2_page_touch_register(void) {
  x86_register_override("XMen2.exe", kOnMouse, x2_override_005bb590);
}
