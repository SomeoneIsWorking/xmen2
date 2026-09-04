#include "guest_body.h"
#include <lucent/log_c.h>

#include "x86_engine.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>

int x86_guest_body_try(CPU *C, const char *module, uint32_t linked_ep,
                       char *why, unsigned why_len) {
  uint32_t mapped = 0;
  if (x86_override_resolve_check(module, linked_ep, &mapped, why, why_len) != 0)
    return 0;
  return x2_engine_call(mapped, C) ? 1 : 0;
}

void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  char why[256];
  uint32_t mapped = 0;

  if (x86_override_resolve_check(module, linked_ep, &mapped, why, sizeof why) !=
      0) {
    lucent_log_error(
        "x2",
        "x86_guest_body: %s 0x%08x could not be resolved: %s\n"
        "    A native override asked for the guest's own body and\n"
        "    there is no address to run it at. Continuing would skip\n"
        "    the function entirely.\n",
        module, linked_ep, why);
    abort();
  }
  if (!x2_engine_call(mapped, C)) {
    lucent_log_error(
        "x2",
        "x86_guest_body: the execution engine declined %s 0x%08x "
        "(mapped 0x%08x).\n"
        "    It is the only thing that can run guest bytes in this "
        "build, so there is no second answer to fall back on.\n",
        module, linked_ep, mapped);
    abort();
  }
}
