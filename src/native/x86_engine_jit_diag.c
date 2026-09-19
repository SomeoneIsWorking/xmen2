#include "x86_engine_jit_diag.h"

#include "jit_engine.h"
#include "threads.h"
#include "x86rt_native.h"

#include "cpu.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>

/*
 * One report of an entry to the watched address.
 *
 * Says what a wedge needs and cannot otherwise get: which block sent the run
 * here, what the register file held on arrival, and what this thread last
 * asked the host for. Every one of those is printed even when it is empty --
 * "no previous block" and "never crossed" are findings, and a line that
 * appeared only when they were non-empty could not tell them from a report
 * that never ran.
 */
static void watch_report(void *user, uint32_t addr, uint32_t previous,
                         int have_previous, X86pCpu *cpu) {
  const char *what = NULL;
  uint32_t import_at = 0;
  double ago = 0.0;
  const char *name = x86_native_name_at(addr);
  unsigned long *seen = (unsigned long *)user;
  (*seen)++;
  lucent_log_error("engine", "jit.watch: entry %lu to guest 0x%08x (%s)", *seen,
                   addr, name ? name : "unnamed");
  if (have_previous) {
    const char *prev_name = x86_native_name_at(previous);
    lucent_log_error("engine", "jit.watch:   came from block 0x%08x (%s)",
                     previous, prev_name ? prev_name : "unnamed");
  } else {
    lucent_log_error("engine", "jit.watch:   NO previous block -- this is the "
                               "first block this run entered");
  }
  lucent_log_error("engine", "jit.watch:   eax=%08x ecx=%08x edx=%08x ebx=%08x",
                   cpu->reg[kX86pEax], cpu->reg[kX86pEcx], cpu->reg[kX86pEdx],
                   cpu->reg[kX86pEbx]);
  lucent_log_error("engine", "jit.watch:   esp=%08x ebp=%08x esi=%08x edi=%08x",
                   cpu->reg[kX86pEsp], cpu->reg[kX86pEbp], cpu->reg[kX86pEsi],
                   cpu->reg[kX86pEdi]);
  if (guest_thread_last_crossing(&what, &import_at, &ago)) {
    lucent_log_error("engine",
                     "jit.watch:   this thread last crossed into %s (thunk "
                     "0x%08x), %.3fs ago",
                     what, import_at, ago);
  } else {
    lucent_log_error("engine",
                     "jit.watch:   this thread has never crossed the host "
                     "boundary, so it has only ever run compiled guest code");
  }
}

static unsigned long g_watch_seen;

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
  {
    long slots = lucent_cvar_number("jit.chain", 0);
    if (slots > 0) {
      if (!x86p_jit_engine_set_chain_census(jit, 1, (uint32_t)slots, reason,
                                            reason_len))
        return 0;
      lucent_log_info("engine",
                      "jit.chain=%ld: the runtime chain census is armed; how "
                      "many dispatches went to an address the block just left "
                      "already knew prints at shutdown",
                      slots);
    }
  }
  {
    long addr = lucent_cvar_number("jit.watch", 0);
    if (addr > 0) {
      long reports = lucent_cvar_number("jit.watchn", 4);
      if (reports <= 0) {
        reports = 4;
      }
      x86p_jit_engine_set_entry_watch(jit, (uint32_t)addr, (uint64_t)reports,
                                      watch_report, &g_watch_seen);
      lucent_log_info("engine",
                      "jit.watch=0x%08lx: the first %ld entries to that guest "
                      "address report the block they came from and the "
                      "register file",
                      (unsigned long)addr, reports);
    }
  }
  return 1;
}
