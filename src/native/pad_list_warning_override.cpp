/*
 * The retail "gamepads have been added/removed" warning predicate (0x00618e80),
 * answered no: the port assigns controllers itself, and its own touch pad
 * attaching counts as a list change. Its one caller is the menu manager's
 * update at 0x005db0cb, which runs the gamepadchanged script on yes.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kPadListWarning = 0x00618e80u;

} // namespace

extern "C" void x2_override_00618e80(CPU *cpu) {
  cpu->reg[kX86pEax] = 0u;
  /* __cdecl, no args. */
  cpu->reg[kX86pEsp] += 4u;
}

__attribute__((constructor)) static void x2_pad_list_warning_register(void) {
  x86_register_override("XMen2.exe", kPadListWarning, x2_override_00618e80);
}
