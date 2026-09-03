#include "x86_engine_jit_diag.h"

#include "jit_engine.h"

#include <lucent/cvar_c.h>

#include <stdio.h>

void x86_engine_jit_diag_configure(struct X86pJitEngine *jit) {
  if (!jit)
    return;
  if (!lucent_cvar_flag("jit.cache", 1)) {
    x86p_jit_engine_set_cache(jit, 0);
    fprintf(stderr, "[ENGINE] jit.cache=off: block cache disabled\n");
  }
  if (lucent_cvar_flag("jit.verify", 0)) {
    x86p_jit_engine_set_verify(jit, 1);
    fprintf(stderr,
            "[ENGINE] jit.verify=on: every block entry is cross-checked "
            "against the interpreter\n");
  }
  {
    long slots = lucent_cvar_number("jit.profile", 0);
    if (slots > 0) {
      x86p_jit_engine_set_profile(jit, 1, (uint32_t)slots);
      fprintf(stderr,
              "[ENGINE] jit.profile=%ld: block-entry histogram armed; the "
              "hottest blocks print at shutdown\n",
              slots);
    }
  }
}

void x86_engine_jit_diag_report(const struct X86pJitEngineStats *stats) {
  if (!stats ||
      (!stats->verify_blocks_checked && !stats->verify_blocks_skipped))
    return;
  fprintf(stderr,
          "[ENGINE] jit verify: %llu block entr(ies) cross-checked against the "
          "interpreter and agreed, %llu skipped (REP string op, shadow fault, "
          "or undo-log overflow)\n",
          (unsigned long long)stats->verify_blocks_checked,
          (unsigned long long)stats->verify_blocks_skipped);
}
