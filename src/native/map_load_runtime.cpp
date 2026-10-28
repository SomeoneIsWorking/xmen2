#include "boot_blackout.h"
#include "guest_body.h"
#include "lan_session.h"
#include "save_trace_runtime.h"
#include "x86rt.h"
#include "x86rt_native.h"

namespace x2::native {
namespace {

/* FUN_00484ce0 loads a map (ECX); AL is its success. */
void map_load_override_00484ce0(CPU *C) {
  const uint32_t map = C->reg[kX86pEcx];

  x86_guest_body(C, "XMen2.exe", 0x00484ce0u);
  const int succeeded = (C->reg[kX86pEax] & 0xffu) != 0u;
  x2::save::save_trace_map_return(map, succeeded);
  lan_session_map_loaded(map, succeeded);
  /* The boot's own destination load ends the boot blackout; later loads are
     no-ops to it. */
  if (succeeded)
    x2::presentation::boot_blackout_disarm("the boot's map load returned");
}

__attribute__((constructor)) void map_load_register() {
  x86_register_override("XMen2.exe", 0x00484ce0u, map_load_override_00484ce0);
}

} // namespace
} // namespace x2::native
