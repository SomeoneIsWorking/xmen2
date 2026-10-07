/* CMenuWorldMap's init (0x005e8c40) with its second Back hidden, a deliberate
   difference from retail; only CMenuItem::parse sets the hidden bit in-game. */
#include "guest_body.h"
#include "guest_memory.h"
#include "retail_menu_model.hpp"
#include "world_map_footer.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/log_c.h>

#include <cstdint>

namespace {

constexpr std::uint32_t kWorldMapInit = 0x005e8c40u;
constexpr std::uint32_t kItemFlags = 0x54u;

} // namespace

extern "C" void x2_override_005e8c40(CPU *cpu) {
  const std::uint32_t menu = cpu->reg[kX86pEcx];
  x86_guest_body(cpu, "XMen2.exe", kWorldMapInit);
  const x2::native::LiveGuestMemory memory;
  x2::menu::RetailMenuModel model(memory, x86_module_base("XMen2.exe"));
  x2::menu::MenuSnapshot snapshot;
  if (model.read_menu(menu, &snapshot) != x2::menu::ReadStatus::ok) {
    lucent_log_error("menu", "world map 0x%08x unreadable, both Backs stay",
                     menu);
    return;
  }
  const auto item = x2::native::secondary_back_footer(snapshot);
  if (!item) {
    return;
  }
  std::uint8_t flags = 0;
  guest_memory_read(*item + kItemFlags, &flags, 1u);
  flags = static_cast<std::uint8_t>(flags | x2::menu::kItemHidden);
  guest_memory_write(*item + kItemFlags, &flags, 1u);
}

__attribute__((constructor)) static void x2_world_map_footer_register(void) {
  x86_register_override("XMen2.exe", kWorldMapInit, x2_override_005e8c40);
}
