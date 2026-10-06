/*
 * The storage-device check (XMen2.exe FUN_004aee50), without its Xbox settle
 * delay.
 *
 * Every save, load and level transition asks the storage-device manager
 * (FUN_0055e9a0) whether the chosen device is still there. Before asking, the
 * function spins on the game timer until 0.7 s ([0x006840ac]) have passed --
 * time an Xbox memory unit was given to settle. On PC the manager is a stub
 * whose answers are constants (its vtable slots +0x8 and +0xc return 0 and 1),
 * so the spin waits for nothing, with no frame drawn: two such checks froze the
 * loading screen for 702 ms each on every level load.
 *
 * The override runs the prologue the spin sits behind -- push ecx (the spin's
 * local), push esi, push edi, mov esi, ecx -- and resumes the retail body at
 * 0x004aee86, the first instruction after the spin. Nothing past it reads the
 * spin's local or the registers it clobbered, so the device logic that follows
 * runs exactly as retail does.
 */
extern "C" {
#include "guest_body.h"
#include "x2_log.h"
#include "x86rt_native.h"
}

#include <cstdint>

namespace x2::storage {
namespace {

inline constexpr uint32_t kDeviceCheck = 0x004aee50u;
inline constexpr uint32_t kAfterSettleSpin = 0x004aee86u;

class DeviceCheck {
public:
  /* FUN_004aee50 is __thiscall with no stack arguments; ECX is the menu. */
  void run(CPU *C) {
    const uint32_t frame_esp = C->reg[kX86pEsp];
    push(C, C->reg[kX86pEcx]);
    push(C, C->reg[kX86pEsi]);
    push(C, C->reg[kX86pEdi]);
    C->reg[kX86pEsi] = C->reg[kX86pEcx];
    if (checks_++ == 0u) {
      x2_log_info("storage: device checks skip the retail 0.7 s Xbox settle "
                  "spin (FUN_004aee50); the PC device manager's answers do "
                  "not depend on it\n");
    }
    x86_guest_body_resume(C, "XMen2.exe", kAfterSettleSpin, frame_esp);
  }

private:
  static void push(CPU *C, uint32_t value) {
    C->reg[kX86pEsp] -= 4u;
    WR32(C->reg[kX86pEsp], value);
  }

  unsigned long checks_ = 0;
};

DeviceCheck g_device_check;

void device_check(CPU *C) { g_device_check.run(C); }

__attribute__((constructor)) void register_device_check() {
  x86_register_override("XMen2.exe", kDeviceCheck, device_check);
}

} // namespace
} // namespace x2::storage
