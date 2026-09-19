#include "x86_engine_report.h"

#include "guest_memory.h"
#include "x86rt_native.h"

#include "jit_engine.h"
#include "jit_profile.h"

#include <lucent/log_c.h>

#include <stdatomic.h>
#include <stdio.h>

static atomic_int g_live_requested = 1;

int x86_engine_report_request(void) {
  return atomic_exchange_explicit(&g_live_requested, 1, memory_order_relaxed);
}

/*
 * Why translated code keeps being thrown away, split by who threw it.
 *
 * `blocks_translated` climbing with no cache flushes says code is being
 * invalidated and translated again, and on its own says nothing about who is
 * asking. There are two possible owners and they want opposite fixes:
 *
 *   - THIS PORT telling the engine that guest memory changed. The engine
 *     reports the calls it received and the blocks they dropped; guest memory
 *     reports which of its three operations sent them. Calls without drops are
 *     notifications over memory that held no code -- pure cost, and the only
 *     way to see that is to print the zero.
 *   - THE ENGINE reclaiming its own code arena because it is full. Every
 *     eviction is a block the run is about to translate again, and the fix is
 *     the arena size, not this port's notifications.
 *
 * They are printed apart because a single combined figure accused the wrong
 * one: measured on the Dead Zone route, 500 of 106,000 were this port's.
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
                  "[HB] this port invalidated: %llu call(s) over %llu guest "
                  "byte(s) dropped %llu block(s); asked by %s",
                  (unsigned long long)js->invalidations,
                  (unsigned long long)js->invalidation_bytes,
                  (unsigned long long)js->invalidation_blocks_dropped,
                  by_cause);
  /*
   * The zero has to read as an answer, not as a missing sentence. "Evicted 0
   * times, the arena is too small by exactly that much" is the phrasing a
   * count-and-suffix line produces and it says the opposite of what zero
   * means, so the two cases are worded apart.
   */
  lucent_log_info("engine",
                  js->evictions
                      ? "[HB] the engine evicted: %llu time(s) dropping %llu "
                        "block(s) of %llu translated -- the code arena is too "
                        "small for the working set by exactly that much"
                      : "[HB] the engine evicted: %llu time(s), %llu block(s), "
                        "of %llu translated -- the code arena holds the whole "
                        "working set reached so far",
                  (unsigned long long)js->evictions,
                  (unsigned long long)js->eviction_blocks_dropped,
                  (unsigned long long)js->blocks_translated);
}

void x86_engine_report_hot_blocks(const X86EngineJitPool *jit,
                                  const char *tag) {
  x86_engine_report_hot_blocks_from(
      jit ? x86p_jit_engine_profile(x86_engine_jit_pool_primary(jit)) : NULL,
      tag);
}

void x86_engine_report_hot_blocks_from(const X86pJitProfile *profile,
                                       const char *tag) {
  X86pJitProfileEntry top[40];
  uint64_t total;
  uint32_t count;
  uint32_t i;
  if (!profile) {
    return;
  }
  total = x86p_jit_profile_total_hits(profile);
  if (x86p_jit_profile_distinct(profile) == 0u || total == 0u) {
    lucent_log_info("engine",
                    "%sJIT hot blocks: the histogram is armed and has recorded "
                    "no block entry at all, so nothing here has executed "
                    "translated code",
                    tag);
    return;
  }
  count = x86p_jit_profile_top(profile, top, 40u);
  lucent_log_info(
      "engine",
      "%sJIT hot blocks: %u distinct, %llu entries total, %llu "
      "key(s) dropped (table full; tail under-counted), top %u "
      "follows",
      tag, x86p_jit_profile_distinct(profile), (unsigned long long)total,
      (unsigned long long)x86p_jit_profile_dropped_keys(profile), count);
  for (i = 0; i < count; i++) {
    const char *name = x86_native_name_at(top[i].guest_eip);
    lucent_log_info("engine", "%s%2u. 0x%08x %-40s %10llu %5.1f%%", tag, i + 1u,
                    top[i].guest_eip, name ? name : "unnamed",
                    (unsigned long long)top[i].entries,
                    100.0 * (double)top[i].entries / (double)total);
  }
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
      "lowered inline (%llu unrecorded predecessor, %llu underivable kind); "
      "product fallback unavailable",
      (unsigned long long)js.blocks_entered,
      (unsigned long long)js.blocks_translated,
      (unsigned long long)js.guest_insns_translated, callouts,
      (unsigned long long)js.translate_refusals,
      (unsigned long long)(js.blocks_translated + js.translate_refusals),
      (unsigned long long)js.cache_flushes,
      (unsigned long long)js.code_bytes_used,
      (unsigned long long)js.conds_inline,
      (unsigned long long)js.conds_translated,
      (unsigned long long)js.conds_unknown_kind,
      (unsigned long long)(js.conds_translated - js.conds_inline -
                           js.conds_unknown_kind));
  report_invalidation(&js);
  x86_engine_report_hot_blocks(jit, "[HB] ");
}
