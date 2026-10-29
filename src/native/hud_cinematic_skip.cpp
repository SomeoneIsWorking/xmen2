#include "hud_cinematic_skip.h"

#include "guest_body.h"
#include "guest_memory.h"
#include "guest_modules.h"
#include "x86rt_native.h"

#include <cstdint>
#include <cstring>

namespace x2::native {
namespace {

/* CHud, the static the getter 0x0059ee20 constructs once. */
constexpr uint32_t kHud = 0x0081d7e0u;
constexpr uint32_t kHudGuard = 0x008a6194u;
/* Set by startCinematic 0x0059b3b0, cleared by endCinematic 0x00599da0. */
constexpr uint32_t kCinematicFlags = 0x889b1u;
constexpr uint8_t kCinematicShown = 0x4u;
/* startCinematic's second argument, 0x80 bytes. */
constexpr uint32_t kCancelScript = 0x8890cu;
constexpr uint32_t kHudUpdate = 0x0059f1a0u;
/* Input vtable +0x140: the action mask the update tests for bits 4 and 20. */
constexpr uint32_t kActionMask = 0x005d4b00u;
constexpr uint32_t kSkipAction = 20u;

class HudCinematicSkip {
public:
  bool skippable() const {
    uint8_t guard = 0;
    uint8_t flags = 0;
    uint8_t script = 0;
    return guest_memory_try_read(exe_linked(kHudGuard), &guard, 1) &&
           (guard & 1u) &&
           guest_memory_try_read(exe_linked(kHud) + kCinematicFlags, &flags,
                                 1) &&
           (flags & kCinematicShown) &&
           guest_memory_try_read(exe_linked(kHud) + kCancelScript, &script,
                                 1) &&
           script != 0;
  }

  void press() {
    pending_ = true;
    ++presses_;
  }

  void update(CPU *cpu) {
    /* A press the cinematic outlived is dropped, not kept for the next one. */
    forcing_ =
        pending_ && cpu->reg[kX86pEcx] == exe_linked(kHud) && skippable();
    pending_ = false;
    x86_guest_body(cpu, "XMen2.exe", kHudUpdate);
    forcing_ = false;
  }

  void action_mask(CPU *cpu) {
    x86_guest_body(cpu, "XMen2.exe", kActionMask);
    if (forcing_) {
      cpu->reg[kX86pEax] |= 1u << kSkipAction;
      ++forced_;
    }
  }

  void snapshot(HudCinematicSkipSnapshot *out) const {
    std::memset(out, 0, sizeof *out);
    uint8_t guard = 0;
    uint8_t flags = 0;
    if (guest_memory_try_read(exe_linked(kHudGuard), &guard, 1) &&
        (guard & 1u) &&
        guest_memory_try_read(exe_linked(kHud) + kCinematicFlags, &flags, 1)) {
      out->shown = (flags & kCinematicShown) != 0;
      (void)guest_memory_try_read(exe_linked(kHud) + kCancelScript,
                                  out->cancel_script,
                                  sizeof out->cancel_script - 1);
    }
    out->presses = presses_;
    out->forced = forced_;
  }

private:
  bool pending_ = false;
  bool forcing_ = false;
  unsigned long presses_ = 0;
  unsigned long forced_ = 0;
};

HudCinematicSkip skip;

void hud_update(CPU *cpu) { skip.update(cpu); }
void action_mask(CPU *cpu) { skip.action_mask(cpu); }

} // namespace

bool hud_cinematic_skippable() { return skip.skippable(); }

void hud_cinematic_skip_press() { skip.press(); }

void hud_cinematic_skip_snapshot(HudCinematicSkipSnapshot *out) {
  skip.snapshot(out);
}

} // namespace x2::native

__attribute__((constructor)) static void register_hud_cinematic_skip() {
  x86_register_override("XMen2.exe", x2::native::kHudUpdate,
                        x2::native::hud_update);
  x86_register_override("XMen2.exe", x2::native::kActionMask,
                        x2::native::action_mask);
}
