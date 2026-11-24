/*
 * CMenuOptions without its way into Advanced Options (plan #203): the port's
 * settings are rows of Options itself, so Start no longer opens CMenuSebas
 * and the "$MENU_OK Advanced Options" footer is not shown.
 *
 * The update (0x005cbdf0, vtable 0x0069ebd4 +0x38) runs its leading part
 * natively -- the early return while the menu waits, then the pressed mask --
 * and resumes the retail body at 0x005cbe73, past the Start branch that set
 * 0x008afde4 and ran "openmenu sebas". The init (0x005cb9c0, +0x44) runs as
 * retail and then clears the footer slot it filled.
 * external/xmen2-decomp/docs/notes/menus.md.
 */
#include "guest_body.h"
#include "guest_memory.h"
#include "guest_modules.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "xmen2_symbols.hpp"

#include <cstdint>

namespace x2::native {
namespace {

namespace exe = x2::symbols::xmen2;

inline constexpr std::uint32_t kWaitFlags = 0x18e4u;
inline constexpr std::uint32_t kVtGameStateBusy = 0x18u;
inline constexpr std::uint32_t kVtPromptUp = 0x78u;
inline constexpr std::uint32_t kVtManagerWait = 0x74u;
inline constexpr std::uint32_t kVtManagerPressedMask = 0x140u;
/* CMenu's footer texts (desctext); slot 3 is the one the
   init writes "$MENU_OK Advanced Options" to (0x005cbbc7). */
inline constexpr std::uint32_t kFooters = 0x2f4u;
inline constexpr std::uint32_t kAdvancedFooter = 3u;

class OptionsWithoutAdvanced {
public:
  void update(CPU *C) const {
    const std::uint32_t menu = C->reg[kX86pEcx];
    if ((RD8(menu + kWaitFlags) & 1u) != 0u && !busy(*C)) {
      /* Before the pushes: a guest call from *C writes its return address
         where the second argument goes. */
      const std::uint32_t menu_manager = manager(*C);
      CPU call = *C;
      push(&call, 0u);
      push(&call, 0u);
      call.reg[kX86pEcx] = menu_manager;
      x86_guest_call_args(&call, method(call.reg[kX86pEcx], kVtManagerWait),
                          8u);
      C->reg[kX86pEax] = call.reg[kX86pEax];
      C->reg[kX86pEsp] += 4u;
      return;
    }
    const std::uint32_t frame_esp = C->reg[kX86pEsp];
    push(C, C->reg[kX86pEdi]);
    push(C, C->reg[kX86pEsi]);
    C->reg[kX86pEdi] = menu;
    C->reg[kX86pEsi] = call_this(*C, manager(*C), kVtManagerPressedMask);
    x86_guest_body_resume(C, "XMen2.exe", exe::MenuOptions_UpdateAfterStart,
                          frame_esp);
  }

  void init(CPU *C) const {
    const std::uint32_t menu = C->reg[kX86pEcx];
    x86_guest_body(C, "XMen2.exe", exe::MenuOptions_Init);
    CPU call = *C;
    push(&call, 0u);
    push(&call, kAdvancedFooter);
    call.reg[kX86pEcx] = menu + kFooters;
    x86_guest_call_args(&call, exe_linked(exe::MenuFooters_Set), 8u);
  }

private:
  /* 0x005cbdfc..0x005cbe1a: the game state or a prompt holds the menu. */
  [[nodiscard]] bool busy(const CPU &from) const {
    CPU probe = from;
    const std::uint32_t state = call_free(&probe, exe::GameState_Get);
    if (call_this(probe, state, kVtGameStateBusy) != 0u) {
      return true;
    }
    const std::uint32_t prompt = call_free(&probe, exe::Prompt_Get);
    return (call_this(probe, prompt, kVtPromptUp) & 0xffu) != 0u;
  }

  [[nodiscard]] std::uint32_t manager(const CPU &from) const {
    CPU probe = from;
    return call_free(&probe, exe::MenuMgr_Get);
  }

  [[nodiscard]] std::uint32_t call_free(CPU *C, std::uint32_t preferred) const {
    x86_guest_call_args(C, exe_linked(preferred), 0u);
    return C->reg[kX86pEax];
  }

  static std::uint32_t method(std::uint32_t object, std::uint32_t slot) {
    return RD32(RD32(object) + slot);
  }

  static std::uint32_t call_this(const CPU &from, std::uint32_t object,
                                 std::uint32_t slot) {
    CPU call = from;
    call.reg[kX86pEcx] = object;
    x86_guest_call_args(&call, method(object, slot), 0u);
    return call.reg[kX86pEax];
  }

  static void push(CPU *C, std::uint32_t value) {
    C->reg[kX86pEsp] -= 4u;
    WR32(C->reg[kX86pEsp], value);
  }
};

OptionsWithoutAdvanced g_options_without_advanced;

void options_update(CPU *C) { g_options_without_advanced.update(C); }

void options_init(CPU *C) { g_options_without_advanced.init(C); }

__attribute__((constructor)) void register_options_without_advanced() {
  x86_register_override("XMen2.exe", exe::MenuOptions_Update, options_update);
  x86_register_override("XMen2.exe", exe::MenuOptions_Init, options_init);
}

} // namespace
} // namespace x2::native
