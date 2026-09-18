#include "x86_engine_report.h"

#include "guest_memory.h"

#include "jit_engine.h"

#include <lucent/log_c.h>

#include <stdatomic.h>
#include <stdio.h>

static atomic_int g_live_requested = 1;

int x86_engine_report_request(void) {
  return atomic_exchange_explicit(&g_live_requested, 1, memory_order_relaxed);
}

/*
 * Why translated code keeps being thrown away, in both halves.
 *
 * `blocks_translated` climbing with no cache flushes says code is being
 * invalidated and translated again, and on its own says nothing about who is
 * asking. The engine reports the calls it received and the blocks they
 * actually dropped; guest memory reports which of its three operations sent
 * them. Calls without drops are notifications over memory that held no code --
 * pure cost, and the only way to see that is to print the zero.
 */
static void report_invalidation(const X86pJitEngineStats *js) {
  const GuestMemoryRemapCounts remaps = guest_memory_remap_counts();
  char by_cause[128];
  int used = 0;
  int cause;
  for (cause = 0; cause < kGuestRemapCauseCount; cause++) {
    const int written =
        snprintf(by_cause + used, sizeof by_cause - (size_t)used,
                 "%s%s %llu/%llu pg", used ? ", " : "",
                 guest_memory_remap_cause_name((GuestMemoryRemapCause)cause),
                 (unsigned long long)remaps.calls[cause],
                 (unsigned long long)remaps.pages[cause]);
    if (written < 0 || (size_t)written >= sizeof by_cause - (size_t)used) {
      break;
    }
    used += written;
  }
  lucent_log_info("engine",
                  "[HB] invalidation: %llu call(s) over %llu guest byte(s) "
                  "dropped %llu block(s) of %llu translated; asked by %s",
                  (unsigned long long)js->invalidations,
                  (unsigned long long)js->invalidation_bytes,
                  (unsigned long long)js->invalidation_blocks_dropped,
                  (unsigned long long)js->blocks_translated, by_cause);
}

void x86_engine_report_live_if_requested(const X86EngineJitPool *jit,
                                         unsigned long callouts) {
  X86pJitEngineStats js = {0};
  if (!atomic_load_explicit(&g_live_requested, memory_order_relaxed) ||
      !atomic_exchange_explicit(&g_live_requested, 0, memory_order_relaxed)) {
    return;
  }
  if (jit) {
    x86_engine_jit_pool_stats(jit, &js);
  }
  lucent_log_info(
      "engine",
      "[HB] JIT: %llu blocks entered, %llu translated (%llu instructions); "
      "%lu native hand-backs; %llu refusals of %llu translation attempts; "
      "%llu cache flushes, %llu bytes code; %llu of %llu condition(s) "
      "lowered inline; product fallback unavailable",
      (unsigned long long)js.blocks_entered,
      (unsigned long long)js.blocks_translated,
      (unsigned long long)js.guest_insns_translated, callouts,
      (unsigned long long)js.translate_refusals,
      (unsigned long long)(js.blocks_translated + js.translate_refusals),
      (unsigned long long)js.cache_flushes,
      (unsigned long long)js.code_bytes_used,
      (unsigned long long)js.conds_inline,
      (unsigned long long)js.conds_translated);
  report_invalidation(&js);
}
