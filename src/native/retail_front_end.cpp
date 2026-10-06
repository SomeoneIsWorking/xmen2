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

/* FUN_005eb300: the popup manager (vtable 0x006a332c). It stacks up to
   three popups; +0x403c is the current one, each record is 0x1560 bytes from
   +0x18. A record's +0x155d bit 0 says it is open, +0x1558 counts its
   options, and option i's script is a 0x80-byte string at +0x901 + i * 0x80,
   which the popup's input handler (FUN_005eb320) runs on accept before it
   closes the popup. Slot 0x34 (FUN_005e99e0) focuses an option. */
inline constexpr uint32_t kPopupManagerRva = 0x001eb300u;
inline constexpr uint32_t kPopupCurrent = 0x403cu;
inline constexpr uint32_t kPopupRecords = 0x18u;
inline constexpr uint32_t kPopupRecordBytes = 0x1560u;
inline constexpr uint32_t kPopupRecordCount = 3u;
inline constexpr uint32_t kPopupFlags = 0x155du;
inline constexpr uint8_t kPopupOpen = 0x01u;
inline constexpr uint32_t kPopupOptionCount = 0x1558u;
inline constexpr uint32_t kPopupOptionScripts = 0x901u;
inline constexpr uint32_t kPopupScriptBytes = 0x80u;
inline constexpr uint32_t kPopupFocusSlot = 0x34u;

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
      exe + kMenuItemByNameRva, menu, {name.address()});
  if (!target) {
    return false;
  }
  guest::GuestCall(cpu).virtual_call(menu, kMenuFocusSlot, {target});
  return true;
}

bool FrontEnd::focus_popup_option(const CPU &cpu, std::string_view script) {
  const uint32_t exe = exe_base();
  const uint32_t manager =
      exe ? guest::GuestCall(cpu).cdecl_call(exe + kPopupManagerRva) : 0u;
  if (!manager) {
    return false;
  }
  const auto current = static_cast<int32_t>(RD32(manager + kPopupCurrent));
  const uint32_t record =
      manager + kPopupRecords +
      (current >= 0 && static_cast<uint32_t>(current) < kPopupRecordCount
           ? static_cast<uint32_t>(current) * kPopupRecordBytes
           : 0u);
  if ((RD8(record + kPopupFlags) & kPopupOpen) == 0u) {
    return false;
  }
  const auto options = static_cast<int8_t>(RD8(record + kPopupOptionCount));
  for (int8_t option = 0; option < options; ++option) {
    const uint32_t at = record + kPopupOptionScripts +
                        static_cast<uint32_t>(option) * kPopupScriptBytes;
    if (guest::guest_string(at, kPopupScriptBytes) == script) {
      return (guest::GuestCall(cpu).virtual_call(
                  manager, kPopupFocusSlot, {static_cast<uint32_t>(option)}) &
              0xffu) != 0u;
    }
  }
  return false;
}

} // namespace x2::retail
