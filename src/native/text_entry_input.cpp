/*
 * CMenuTextEntry's typing (docs/RE/menus.md): its update takes the one
 * character the game's window callback stored for a WM_CHAR, so a word that
 * arrives at once, as a phone's keyboard commits it, is handed over one
 * character per update. While the menu is up the platform's text input is
 * on, which on a phone is its screen keyboard; it goes off when the menu
 * closes.
 */
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "win32_events.h"
#include "win32_mouse.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>

namespace {

using x2::native::exe_linked;

/* CMenuTextEntry's update and close (vtable 0x0069f06c +0x38, +0x18). */
constexpr std::uint32_t kUpdate = 0x005e72f0u;
constexpr std::uint32_t kClose = 0x005e7490u;
/* The character the update takes, 0 once taken. */
constexpr std::uint32_t kTypedChar = 0x00a0a061u;

} // namespace

extern "C" void x2_override_005e72f0(CPU *cpu) {
  x2::native::win32_events_text_input(true);
  if (RD8(exe_linked(kTypedChar)) == 0u) {
    if (const auto code = x2::native::win32_events_take_char()) {
      x2::guest::GuestCall(*cpu).cdecl_call(
          exe_linked(x2::native::kGameWindowEvent),
          {x2::native::win32_events_hwnd(), x2::native::kWmChar, *code, 1u});
    }
  }
  x86_guest_body(cpu, "XMen2.exe", kUpdate);
}

extern "C" void x2_override_005e7490(CPU *cpu) {
  x2::native::win32_events_text_input(false);
  x86_guest_body(cpu, "XMen2.exe", kClose);
}

__attribute__((constructor)) static void x2_text_entry_input_register(void) {
  x86_register_override("XMen2.exe", kUpdate, x2_override_005e72f0);
  x86_register_override("XMen2.exe", kClose, x2_override_005e7490);
}
