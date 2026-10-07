/*
 * CMenu's base Start handler (0x005ad730), with Back as its fallback when a
 * focused item dispatched it for a key that is both. The rule is
 * menu_start_policy; docs/RE/menus.md says why retail loses Escape there.
 */
#include "guest_body.h"
#include "guest_memory.h"
#include "menu_start_policy.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kExePreferred = 0x00400000u;
constexpr std::uint32_t kBaseStartHandler = 0x005ad730u;
/* After CALL [EAX+0x20] at 0x005bc087. */
constexpr std::uint32_t kFocusedItemStartReturn = 0x005bc08au;
constexpr std::uint32_t kMenuManager = 0x005d8920u;
constexpr std::uint32_t kVtMenuStart = 0x20u;
constexpr std::uint32_t kVtMenuBack = 0x24u;
constexpr std::uint32_t kVtManagerPressedMask = 0x140u;
constexpr std::uint32_t kBackAction = 21u;

class GuestMenuStart final : public x2::native::MenuStartDispatch {
public:
  explicit GuestMenuStart(CPU *cpu)
      : cpu_(cpu), menu_(cpu->reg[kX86pEcx]),
        return_to_(RD32(cpu->reg[kX86pEsp])),
        base_(x86_module_base("XMen2.exe")) {}

  std::uint32_t run_start() override {
    x86_guest_body(cpu_, "XMen2.exe", kBaseStartHandler);
    return cpu_->reg[kX86pEax];
  }

  [[nodiscard]] bool start_is_base_handler() const override {
    return RD32(RD32(menu_) + kVtMenuStart) == linked(kBaseStartHandler);
  }

  [[nodiscard]] bool from_focused_item() const override {
    return return_to_ == linked(kFocusedItemStartReturn);
  }

  [[nodiscard]] bool back_pressed() const override {
    const std::uint32_t manager = call_this(linked(kMenuManager), 0u);
    if (manager == 0u) {
      return false;
    }
    const std::uint32_t mask =
        call_this(RD32(RD32(manager) + kVtManagerPressedMask), manager);
    return ((mask >> kBackAction) & 1u) != 0u;
  }

  std::uint32_t run_back() override {
    const std::uint32_t result =
        call_this(RD32(RD32(menu_) + kVtMenuBack), menu_);
    cpu_->reg[kX86pEax] = result;
    return result;
  }

private:
  [[nodiscard]] std::uint32_t linked(std::uint32_t preferred) const {
    return base_ + (preferred - kExePreferred);
  }

  [[nodiscard]] std::uint32_t call_this(std::uint32_t function,
                                        std::uint32_t object) const {
    CPU call = *cpu_;
    call.reg[kX86pEcx] = object;
    x86_guest_call_args(&call, function, 0u);
    return call.reg[kX86pEax];
  }

  CPU *cpu_;
  std::uint32_t menu_;
  std::uint32_t return_to_;
  std::uint32_t base_;
};

} // namespace

extern "C" void x2_override_005ad730(CPU *cpu) {
  GuestMenuStart dispatch(cpu);
  (void)x2::native::resolve_menu_start(dispatch);
}

__attribute__((constructor)) static void x2_menu_start_register(void) {
  x86_register_override("XMen2.exe", kBaseStartHandler, x2_override_005ad730);
}
