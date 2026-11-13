/*
 * CMenuOptions without its way into Advanced Options (plan #203): the port's
 * settings are rows of Options itself, so Start no longer opens CMenuSebas
 * and the "$MENU_OK Advanced Options" footer is not shown.
 *
 * The update (0x005cbdf0, vtable 0x0069ebd4 +0x38) runs its leading part
 * natively -- the early return while the menu waits, then the pressed mask --
 * and resumes the retail body at 0x005cbe73, past the Start branch that set
 * 0x008afde4 and ran "openmenu sebas". The init (0x005cb9c0, +0x44) runs as
 * retail and then clears the footer slot it filled. external/xmen2-decomp/docs/notes/menus.md.
 */
#include "guest_body.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>

namespace x2::native {
namespace {

inline constexpr std::uint32_t kExePreferred = 0x00400000u;
inline constexpr std::uint32_t kUpdate = 0x005cbdf0u;
inline constexpr std::uint32_t kUpdateAfterStart = 0x005cbe73u;
inline constexpr std::uint32_t kInit = 0x005cb9c0u;
inline constexpr std::uint32_t kWaitFlags = 0x18e4u;
inline constexpr std::uint32_t kGameState = 0x004b2880u;
inline constexpr std::uint32_t kVtGameStateBusy = 0x18u;
inline constexpr std::uint32_t kPrompt = 0x005eb300u;
inline constexpr std::uint32_t kVtPromptUp = 0x78u;
inline constexpr std::uint32_t kMenuManager = 0x005d8920u;
inline constexpr std::uint32_t kVtManagerWait = 0x74u;
inline constexpr std::uint32_t kVtManagerPressedMask = 0x140u;
/* CMenu's footer texts (desctext) and their setter; slot 3 is the one the
   init writes "$MENU_OK Advanced Options" to (0x005cbbc7). */
inline constexpr std::uint32_t kFooters = 0x2f4u;
inline constexpr std::uint32_t kSetFooter = 0x005bc6b0u;
inline constexpr std::uint32_t kAdvancedFooter = 3u;

class OptionsWithoutAdvanced {
public:
  void update(CPU *C) const {
    const std::uint32_t menu = C->reg[kX86pEcx];
    if ((RD8(menu + kWaitFlags) & 1u) != 0u && !busy(*C)) {
      CPU call = *C;
      push(&call, 0u);
      push(&call, 0u);
      call.reg[kX86pEcx] = manager(*C);
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
    x86_guest_body_resume(C, "XMen2.exe", kUpdateAfterStart, frame_esp);
  }

  void init(CPU *C) const {
    const std::uint32_t menu = C->reg[kX86pEcx];
    x86_guest_body(C, "XMen2.exe", kInit);
    CPU call = *C;
    push(&call, 0u);
    push(&call, kAdvancedFooter);
    call.reg[kX86pEcx] = menu + kFooters;
    x86_guest_call_args(&call, linked(kSetFooter), 8u);
  }

private:
  /* 0x005cbdfc..0x005cbe1a: the game state or a prompt holds the menu. */
  [[nodiscard]] bool busy(const CPU &from) const {
    CPU probe = from;
    const std::uint32_t state = call_free(&probe, kGameState);
    if (call_this(probe, state, kVtGameStateBusy) != 0u) {
      return true;
    }
    const std::uint32_t prompt = call_free(&probe, kPrompt);
    return (call_this(probe, prompt, kVtPromptUp) & 0xffu) != 0u;
  }

  [[nodiscard]] std::uint32_t manager(const CPU &from) const {
    CPU probe = from;
    return call_free(&probe, kMenuManager);
  }

  [[nodiscard]] std::uint32_t linked(std::uint32_t preferred) const {
    return x86_module_base("XMen2.exe") + (preferred - kExePreferred);
  }

  [[nodiscard]] std::uint32_t call_free(CPU *C, std::uint32_t preferred) const {
    x86_guest_call_args(C, linked(preferred), 0u);
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
  x86_register_override("XMen2.exe", kUpdate, options_update);
  x86_register_override("XMen2.exe", kInit, options_init);
}

} // namespace
} // namespace x2::native
