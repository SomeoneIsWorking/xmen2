#include "retail_front_end.hpp"

#include "guest_call.hpp"

namespace x2::retail {
namespace {

/* FUN_0055c890: the console singleton (vtable 0x0069a81c). */
inline constexpr uint32_t kConsoleRva = 0x0015c890u;
/* Console slot 0x1c (FUN_0055c410) copies the command onto its queue and runs
   it at its own safe point; it refuses only while shutting down (+0x630 ==
   2). Slot 0x18 would run it inline, and a command that loads a map re-enters
   the input poll that is calling. */
inline constexpr uint32_t kConsoleQueueSlot = 0x1cu;
/* FUN_005d8920: the menu manager. Slot 0x210 is the active menu; the menu
   keeps the name it was opened by at +0xc. */
inline constexpr uint32_t kMenuManagerRva = 0x001d8920u;
inline constexpr uint32_t kActiveMenuSlot = 0x210u;
inline constexpr uint32_t kMenuName = 0x0cu;
/* FUN_005adc10(menu, name): the menu's item of that name, or 0. */
inline constexpr uint32_t kMenuItemByNameRva = 0x001adc10u;
/* Menu slot 4 moves the focus to an item (CMenu::SetFocus). */
inline constexpr uint32_t kMenuFocusSlot = 0x4u;

uint32_t exe_base() { return x86_module_base("XMen2.exe"); }

uint32_t active_menu_object(const CPU &cpu, uint32_t exe) {
  guest::GuestCall call(cpu);
  const uint32_t manager = call.cdecl_call(exe + kMenuManagerRva);
  return manager ? guest::GuestCall(cpu).virtual_call(manager, kActiveMenuSlot)
                 : 0u;
}

} // namespace

bool FrontEnd::queue_command(const CPU &cpu, std::string_view command) {
  const uint32_t exe = exe_base();
  const guest::GuestText text(command);
  if (!exe || !text) {
    return false;
  }
  const uint32_t console = guest::GuestCall(cpu).cdecl_call(exe + kConsoleRva);
  return (guest::GuestCall(cpu).virtual_call(console, kConsoleQueueSlot,
                                             {text.address()}) &
          0xffu) != 0u;
}

std::string FrontEnd::active_menu(const CPU &cpu) {
  const uint32_t exe = exe_base();
  const uint32_t menu = exe ? active_menu_object(cpu, exe) : 0u;
  return menu ? std::string(guest::guest_string(menu + kMenuName, 64u))
              : std::string();
}

bool FrontEnd::focus(const CPU &cpu, std::string_view item) {
  const uint32_t exe = exe_base();
  const uint32_t menu = exe ? active_menu_object(cpu, exe) : 0u;
  const guest::GuestText name(item);
  if (!menu || !name) {
    return false;
  }
  const uint32_t target = guest::GuestCall(cpu).thiscall(
      exe + kMenuItemByNameRva, menu, {name.address()});
  if (!target) {
    return false;
  }
  guest::GuestCall(cpu).virtual_call(menu, kMenuFocusSlot, {target});
  return true;
}

} // namespace x2::retail
