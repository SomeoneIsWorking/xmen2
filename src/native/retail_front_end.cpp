#include "retail_front_end.hpp"

#include "guest_call.hpp"
#include "menu_item_memory.hpp"
#include "popup_dialog.hpp"

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
/* Menu slot 4 moves the focus to an item (CMenu::SetFocus). */
inline constexpr uint32_t kMenuFocusSlot = 0x4u;

uint32_t exe_base() { return x86_module_base("XMen2.exe"); }

} // namespace

uint32_t FrontEnd::active_menu_object(const CPU &cpu) {
  const uint32_t exe = exe_base();
  const uint32_t manager =
      exe ? guest::GuestCall(cpu).cdecl_call(exe + kMenuManagerRva) : 0u;
  return manager ? guest::GuestCall(cpu).virtual_call(manager, kActiveMenuSlot)
                 : 0u;
}

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
  const uint32_t menu = active_menu_object(cpu);
  return menu ? std::string(guest::guest_string(menu + kMenuName, 64u))
              : std::string();
}

bool FrontEnd::focus(const CPU &cpu, std::string_view item) {
  const uint32_t exe = exe_base();
  const uint32_t menu = active_menu_object(cpu);
  const guest::GuestText name(item);
  if (!menu || !name) {
    return false;
  }
  const uint32_t target = guest::GuestCall(cpu).thiscall(
      exe + native::menu_item::kItemByNameRva, menu, {name.address()});
  if (!target) {
    return false;
  }
  guest::GuestCall(cpu).virtual_call(menu, kMenuFocusSlot, {target});
  return true;
}

bool FrontEnd::focus_popup_option(const CPU &cpu, std::string_view script) {
  const uint32_t exe = exe_base();
  const uint32_t manager =
      exe ? guest::GuestCall(cpu).cdecl_call(exe + native::popup::kManagerRva)
          : 0u;
  if (!manager) {
    return false;
  }
  /* The popup's input handler (FUN_005eb320) runs the accepted option's
     script before it closes the popup. */
  const uint32_t record =
      manager + native::popup::page_offset(static_cast<int32_t>(
                    RD32(manager + native::popup::kCurrentPage)));
  if ((RD8(record + native::popup::kFlags) & native::popup::kOpen) == 0u) {
    return false;
  }
  const auto options =
      static_cast<int8_t>(RD8(record + native::popup::kEntryCount));
  for (int8_t option = 0; option < options; ++option) {
    const uint32_t at =
        record + native::popup::kScripts +
        static_cast<uint32_t>(option) * native::popup::kEntryBytes;
    if (guest::guest_string(at, native::popup::kEntryBytes) == script) {
      return (guest::GuestCall(cpu).virtual_call(
                  manager, native::popup::kFocusSlot,
                  {static_cast<uint32_t>(option)}) &
              0xffu) != 0u;
    }
  }
  return false;
}

} // namespace x2::retail
