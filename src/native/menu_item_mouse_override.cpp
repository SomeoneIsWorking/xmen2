/*
 * CMenuItem::onMouse (0x005bc1b0), extended to the rows without a usecmd that
 * retail never hit-tests; the rule is menu_item_tap.
 */
#include "guest_body.h"
#include "guest_modules.h"
#include "menu_action.hpp"
#include "menu_item_tap.hpp"
#include "retail_menu_model.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kItemOnMouse = 0x005bc1b0u;

constexpr std::uint32_t kItemFlags = 0x54u;
constexpr std::uint8_t kItemHidden = 0x04u;
constexpr std::uint32_t kItemBoxLeft = 0x70u;
constexpr std::uint32_t kItemBoxY = 0x72u;
constexpr std::uint32_t kItemBoxWidth = 0x74u;
constexpr std::uint32_t kItemBoxHeight = 0x76u;
constexpr std::uint32_t kItemBoxHalfZ = 0x7au;

int field_s16(std::uint32_t item, std::uint32_t offset) {
  return static_cast<std::int16_t>(RD16(item + offset));
}

/* The RECT the retail body tests. */
bool in_box(std::uint32_t item, int x, int z) {
  const int left = field_s16(item, kItemBoxLeft);
  const int half = field_s16(item, kItemBoxHalfZ) / 2;
  const int bottom = field_s16(item, kItemBoxY) + half;
  const int top = bottom - field_s16(item, kItemBoxHeight) - 1;
  const int right = left + field_s16(item, kItemBoxWidth) - 1;
  return x >= left && x < right && z >= top && z < bottom;
}

} // namespace

extern "C" void x2_override_005bc1b0(CPU *cpu) {
  const std::uint32_t item = cpu->reg[kX86pEcx];
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const int x = static_cast<int>(RD32(esp + 4u));
  const int z = static_cast<int>(RD32(esp + 8u));
  const std::uint32_t message = RD32(esp + 16u);
  x86_guest_body(cpu, "XMen2.exe", kItemOnMouse);
  if (cpu->reg[kX86pEax] != 0u ||
      (RD8(item + kItemFlags) & kItemHidden) != 0u || !in_box(item, x, z)) {
    return;
  }
  x2::menu::MenuSnapshot menu;
  std::uint32_t failed = 0u;
  if (x2::menu::read_live_menu(&menu, &failed) != x2::menu::ReadStatus::ok) {
    return;
  }
  int index = -1;
  for (std::size_t i = 0; i < menu.items.size(); ++i) {
    if (menu.items[i].address == item) {
      index = static_cast<int>(i);
    }
  }
  const x2::native::MenuItemTap tap =
      x2::native::menu_item_tap(menu, index, x, z, message);
  if (tap.accept) {
    x2::native::post_menu_action(x2::native::MenuAction::accept);
  }
  cpu->reg[kX86pEax] = tap.hit ? 1u : 0u;
}

__attribute__((constructor)) static void x2_menu_item_mouse_register(void) {
  x86_register_override("XMen2.exe", kItemOnMouse, x2_override_005bc1b0);
}
