/*
 * Retail's one mouse button as a finger drives it, handed straight to the
 * game's window-event callback (0x005faa20) at the top of its frame update
 * (0x00401d70): no Win32 message is queued, peeked or dispatched for it.
 * There a release is seen after the last frame's menu update and before this
 * frame's held-button check (docs/touch-play.md). A press that starts on a
 * touch button drawn over a small item is the pointer at that item until it
 * lifts (x2::input::TouchTargets), with the left button, the right one, no
 * button, or nothing at all for a button the port acts on itself.
 */
#include "../input/touch_runtime.h"
#include "../input/touch_targets.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "touch_menu_source.hpp"
#include "win32_events.h"
#include "win32_mouse.h"
#include "win32_pointer.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>
#include <optional>

namespace {

constexpr std::uint32_t kFrameUpdate = 0x00401d70u;

class TouchPointerDispatch {
public:
  /* Queued touch pointer events in order, up to and including one button
     edge: the game's handlers expect a frame between a press and its
     release (CPopupDialog's update clears the press it re-armed). */
  void deliver(const CPU &cpu, std::uint32_t hwnd) {
    x2::input::TouchTargets &targets = x2::input::touch_targets();
    hover(cpu, hwnd, targets.take_hover());
    targets.next_frame();
    x2::input::TouchPointer pointer{};
    while (x2::input::touch_runtime_take_pointer(&pointer) != 0) {
      std::int32_t x = 0;
      std::int32_t y = 0;
      x2::native::win32_pointer_touch_client(&pointer, &x, &y);
      if (pointer.button_change == 1) {
        aim(targets, x, y);
      }
      const bool lifted = pointer.button_change == 0;
      if (lifted) {
        release(targets, x, y);
      }
      if (aim_) {
        x = aim_->x;
        y = aim_->y;
      }
      if (press_ != x2::input::TouchPress::own) {
        move(cpu, hwnd, x, y);
      }
      if (pointer.button_change >= 0) {
        button(cpu, hwnd, pointer.button_change == 1, x, y);
        if (lifted) {
          aim_.reset();
          press_ = x2::input::TouchPress::click;
        }
        return;
      }
    }
  }

private:
  /* A press at (x, y): on a touch button, its item's point and what it hands
     the game; elsewhere the finger's own point and a click. */
  void aim(x2::input::TouchTargets &targets, std::int32_t x, std::int32_t y) {
    aim_.reset();
    press_ = x2::input::TouchPress::click;
    const auto plane = x2::native::live_scene_plane();
    if (!plane) {
      return;
    }
    const auto target = targets.press(plane->to_scene({x, y}));
    if (target) {
      aim_ = plane->to_client(target->aim);
      press_ = target->press;
    }
  }

  /* A move to a hover the menu asked for, unless a finger holds the
     pointer; sent even where the cursor already is. */
  void hover(const CPU &cpu, std::uint32_t hwnd,
             std::optional<x2::presentation::ScenePoint> at) {
    const auto plane = x2::native::live_scene_plane();
    if (!at || !plane || aim_ || held_ != 0u) {
      return;
    }
    const x2::presentation::ClientPoint client = plane->to_client(*at);
    send(cpu, hwnd, x2::native::kWmMouseMove, client.x, client.y);
    placed_ = true;
    x_ = client.x;
    y_ = client.y;
  }

  static void release(x2::input::TouchTargets &targets, std::int32_t x,
                      std::int32_t y) {
    const auto plane = x2::native::live_scene_plane();
    targets.release(plane ? plane->to_scene({x, y})
                          : x2::presentation::ScenePoint{});
  }

  /* A cursor that has not moved sends no move; a held one would read as a
     drag. */
  void move(const CPU &cpu, std::uint32_t hwnd, std::int32_t x,
            std::int32_t y) {
    if (placed_ && x == x_ && y == y_) {
      return;
    }
    send(cpu, hwnd, x2::native::kWmMouseMove, x, y);
    placed_ = true;
    x_ = x;
    y_ = y;
  }

  /* The left button edge the press hands the game: none for an own
     button. */
  void button(const CPU &cpu, std::uint32_t hwnd, bool down, std::int32_t x,
              std::int32_t y) {
    if (press_ == x2::input::TouchPress::own) {
      held_ = 0u;
      return;
    }
    held_ = down ? x2::native::kMkLButton : 0u;
    send(cpu, hwnd,
         down ? x2::native::kWmLButtonDown : x2::native::kWmLButtonUp, x, y);
  }

  void send(const CPU &cpu, std::uint32_t hwnd, std::uint32_t message,
            std::int32_t x, std::int32_t y) const {
    const std::uint32_t entry =
        x2::native::guest_exe_base() +
        (x2::native::kGameWindowEvent - x2::native::kExePreferred);
    x2::guest::GuestCall(cpu).cdecl_call(
        entry,
        {hwnd, message, held_, x2::native::win32_mouse_pack_point(x, y)});
  }

  std::optional<x2::presentation::ClientPoint> aim_;
  x2::input::TouchPress press_ = x2::input::TouchPress::click;
  /* The MK_ button bits held. */
  std::uint32_t held_ = 0u;
  bool placed_ = false;
  std::int32_t x_ = 0;
  std::int32_t y_ = 0;
};

TouchPointerDispatch g_touch_pointer;

} // namespace

extern "C" void x2_override_00401d70(CPU *cpu) {
  g_touch_pointer.deliver(*cpu, x2::native::win32_events_hwnd());
  x86_guest_body(cpu, "XMen2.exe", kFrameUpdate);
}

__attribute__((constructor)) static void x2_touch_pointer_register(void) {
  x86_register_override("XMen2.exe", kFrameUpdate, x2_override_00401d70);
}
