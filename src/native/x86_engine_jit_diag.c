#include "x86_engine_jit_diag.h"

#include "jit_engine.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>

int x86_engine_jit_diag_configure(struct X86pJitEngine *jit, char *reason,
                                  unsigned reason_len) {
  if (!jit)
    return 0;
  if (!lucent_cvar_flag("jit.cache", 1)) {
    x86p_jit_engine_set_cache(jit, 0);
    lucent_log_info("engine", "jit.cache=off: block cache disabled");
  }
  {
    long slots = lucent_cvar_number("jit.profile", 0);
    if (slots > 0) {
      if (!x86p_jit_engine_set_profile(jit, 1, (uint32_t)slots, reason,
                                       reason_len))
        return 0;
      lucent_log_info("engine",
                      "jit.profile=%ld: block-entry histogram armed; the "
                      "hottest blocks print at shutdown",
                      slots);
    }
  }
  return 1;
}
