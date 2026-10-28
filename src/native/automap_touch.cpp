/*
 * CMenuAutoMap on touch (docs/touch-play.md, docs/RE/menus.md): the map is
 * the game's own screen and a finger moves it the way a phone's map moves.
 * A drag that starts off every touch button keeps the ground under the
 * finger; retail's held-button pan and turn (0x210 and 0x211, steering by
 * the pointer's offset from the middle) are not run for a finger. The
 * Rotate prompt's two buttons (text_item_touch.cpp) turn the map while
 * held, at the rate the update turns it for a full camera axis.
 */
#include "automap_touch.hpp"

#include "../input/touch_runtime.h"
#include "../input/touch_targets.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "retail_menu_model.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace {

using x2::native::exe_linked;
using x2::presentation::ScenePoint;

/* CMenuAutoMap's update and onMouse(x, z, wparam, message, lparam), ret
   0x14 (vtable 0x0069e4c4). */
constexpr std::uint32_t kUpdate = 0x005afbd0u;
constexpr std::uint32_t kOnMouse = 0x005afe30u;
/* The map's bounds (low x, y; high x, y), where it looks (x, y, z) and its
   turn, read at init (FUN_005afae0). */
constexpr std::uint32_t kLow = 0x18dcu;
constexpr std::uint32_t kHigh = 0x18e4u;
constexpr std::uint32_t kAt = 0x18ecu;
constexpr std::uint32_t kTurn = 0x18f8u;
/* The update turns by the camera axis times this. */
constexpr std::uint32_t kTurnStep = 0x00682f98u;
/* The view's map scale, scene units per world unit (FUN_005a0150). */
constexpr std::uint32_t kMapScale = 0x008a61c8u;

constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;
bool on_touch() { return x2::input::touch_runtime_active() != 0; }

float f32(std::uint32_t address) { return static_cast<float>(RDF32(address)); }

class AutomapTouch {
public:
  void turn(int direction) { turn_ = direction; }

  /* The Rotate hold, before the update hands the turn to the view. */
  void update(std::uint32_t self) {
    const int direction = std::exchange(turn_, 0);
    if (direction != 0) {
      WRF32(self + kTurn, f32(self + kTurn) - static_cast<float>(direction) *
                                                  f32(exe_linked(kTurnStep)));
    }
  }

  /* A finger's pointer message. */
  void pointer(std::uint32_t self, ScenePoint point, std::uint32_t message) {
    if (message == kButtonDown) {
      drag_.reset();
      if (!x2::input::touch_targets().holding()) {
        drag_ = Drag{point, x2::native::automap_view(self)};
      }
    } else if (message == kPointerMove && drag_) {
      const x2::input::MapPoint at = x2::input::automap_dragged(
          drag_->view, {point.x - drag_->start.x, point.z - drag_->start.z},
          x2::native::automap_scale());
      WRF32(self + kAt, at.x);
      WRF32(self + kAt + 4u, at.y);
    } else if (message == kButtonUp) {
      drag_.reset();
    }
  }

private:
  struct Drag {
    ScenePoint start;
    x2::input::MapView view;
  };
  std::optional<Drag> drag_;
  int turn_ = 0;
};

AutomapTouch g_automap_touch;

} // namespace

namespace x2::native {

void automap_touch_turn(int direction) { g_automap_touch.turn(direction); }

input::MapView automap_view(std::uint32_t menu) {
  return {{f32(menu + kAt), f32(menu + kAt + 4u)},
          f32(menu + kTurn),
          {f32(menu + kLow), f32(menu + kLow + 4u)},
          {f32(menu + kHigh), f32(menu + kHigh + 4u)}};
}

float automap_scale() { return f32(exe_linked(kMapScale)); }

} // namespace x2::native

extern "C" void x2_override_005afbd0(CPU *cpu) {
  if (on_touch()) {
    g_automap_touch.update(cpu->reg[kX86pEcx]);
  }
  x86_guest_body(cpu, "XMen2.exe", kUpdate);
}

extern "C" void x2_override_005afe30(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const std::uint32_t self = cpu->reg[kX86pEcx];
  if (!on_touch()) {
    x86_guest_body(cpu, "XMen2.exe", kOnMouse);
    return;
  }
  const auto x = static_cast<std::int32_t>(RD32(esp + 4u));
  const auto z = static_cast<std::int32_t>(RD32(esp + 8u));
  const std::uint32_t message = RD32(esp + 16u);
  g_automap_touch.pointer(self, {static_cast<float>(x), static_cast<float>(z)},
                          message);
  /* The footers are the base onMouse's, as retail passes every message. */
  x2::guest::GuestCall call(*cpu);
  const std::uint32_t handled = call.thiscall(
      exe_linked(x2::menu::kMenuOnMouse), self,
      {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z),
       RD32(esp + 12u), message, RD32(esp + 20u)});
  cpu->reg[kX86pEax] = (cpu->reg[kX86pEax] & ~0xffu) | (handled & 0xffu);
  cpu->reg[kX86pEsp] += 4u + 20u;
}

__attribute__((constructor)) static void x2_automap_touch_register(void) {
  x86_register_override("XMen2.exe", kUpdate, x2_override_005afbd0);
  x86_register_override("XMen2.exe", kOnMouse, x2_override_005afe30);
}
