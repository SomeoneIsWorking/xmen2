/*
 * Retail's one mouse button as a finger drives it, handed straight to the
 * game's window-event callback (0x005faa20) at the top of its frame update
 * (0x00401d70): no Win32 message is queued, peeked or dispatched for it.
 * There a release is seen after the last frame's menu update and before this
 * frame's held-button check (docs/touch-play.md). A press that starts on a
 * touch button drawn over a small item is the pointer at that item until it
 * lifts (x2::input::TouchTargets).
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
/* igWin32Window's callback for the game: (hwnd, msg, wparam, lparam),
   __cdecl; mouse messages go on to FUN_005f9eb0. */
constexpr std::uint32_t kGameWindowEvent = 0x005faa20u;

class TouchPointerDispatch {
public:
  /* Queued touch pointer events in order, up to and including one button
     edge: the game's handlers expect a frame between a press and its
     release (CPopupDialog's update clears the press it re-armed). */
  void deliver(const CPU &cpu, std::uint32_t hwnd) {
    x2::input::TouchTargets &targets = x2::input::touch_targets();
    targets.next_frame();
    x2::input::TouchPointer pointer{};
    while (x2::input::touch_runtime_take_pointer(&pointer) != 0) {
      std::int32_t x = 0;
      std::int32_t y = 0;
      x2::native::win32_pointer_touch_client(&pointer, &x, &y);
      if (pointer.button_change == 1) {
        aim_ = aim(targets, x, y);
      }
      if (aim_) {
        x = aim_->x;
        y = aim_->y;
      }
      /* A cursor that has not moved sends no move; a held one would read as
         a drag. */
      if (!placed_ || x != x_ || y != y_) {
        send(cpu, hwnd, x2::native::kWmMouseMove, x, y);
        placed_ = true;
        x_ = x;
        y_ = y;
      }
      if (pointer.button_change >= 0) {
        held_ = pointer.button_change == 1;
        send(cpu, hwnd,
             held_ ? x2::native::kWmLButtonDown : x2::native::kWmLButtonUp, x,
             y);
        if (!held_) {
          targets.release();
          aim_.reset();
        }
        return;
      }
    }
  }

private:
  /* The client point a press at (x, y) is delivered at: a touch button's
     item, or none for the finger's own. */
  static std::optional<x2::presentation::ClientPoint>
  aim(x2::input::TouchTargets &targets, std::int32_t x, std::int32_t y) {
    const auto plane = x2::native::live_scene_plane();
    if (!plane) {
      return std::nullopt;
    }
    const auto at = targets.press(plane->to_scene({x, y}));
    if (!at) {
      return std::nullopt;
    }
    return plane->to_client(*at);
  }

  void send(const CPU &cpu, std::uint32_t hwnd, std::uint32_t message,
            std::int32_t x, std::int32_t y) const {
    const std::uint32_t entry = x2::native::guest_exe_base() +
                                (kGameWindowEvent - x2::native::kExePreferred);
    x2::guest::GuestCall(cpu).cdecl_call(
        entry, {hwnd, message, held_ ? x2::native::kMkLButton : 0u,
                x2::native::win32_mouse_pack_point(x, y)});
  }

  std::optional<x2::presentation::ClientPoint> aim_;
  bool held_ = false;
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
