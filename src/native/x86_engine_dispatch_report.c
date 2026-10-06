#include "x86_engine_dispatch_report.h"

#include "x86rt_native.h"

#include "jit_chain_census.h"
#include "jit_engine.h"
#include "jit_profile.h"

#include <lucent/log_c.h>

/*
 * Issue #166. The static exit census says how many of a block's exits NAME a
 * constant successor; this says how many of the dispatches actually paid went
 * to one the block just left had already emitted. That second number is the
 * population general block chaining would remove, and it is the only one worth
 * sizing the work from.
 *
 * `unrecorded` is printed beside it rather than folded into the complement,
 * because a backend that emits no constant successors at all -- which is every
 * machine-code backend here -- would otherwise report a run in which nothing
 * is chainable, and that reads exactly like a run in which chaining is
 * pointless.
 */
void x86_engine_report_chain_census(const X86EngineJitPool *jit,
                                    const char *tag) {
  const X86pJitChainCensus *census =
      jit ? x86p_jit_engine_chain_census(x86_engine_jit_pool_primary(jit))
          : NULL;
  uint64_t entries;
  if (!census) {
    return;
  }
  entries = x86p_jit_chain_census_entries(census);
  if (entries == 0u) {
    lucent_log_info("engine",
                    "%sJIT chaining: the census is armed and counted no block "
                    "entry at all, so this run says nothing about chaining",
                    tag);
    return;
  }
  lucent_log_info(
      "engine",
      "%sJIT chaining: %llu of %llu dispatch(es) went to an address the "
      "previous block already knew (%.1f%%); %llu had no recorded "
      "predecessor (%.1f%%) -- a backend that emits no constant successors "
      "reports every entry here; %u block(s) recorded, %llu key(s) dropped, "
      "%llu successor(s) past the per-block cap",
      tag, (unsigned long long)x86p_jit_chain_census_chainable(census),
      (unsigned long long)entries,
      100.0 * (double)x86p_jit_chain_census_chainable(census) / (double)entries,
      (unsigned long long)x86p_jit_chain_census_unrecorded(census),
      100.0 * (double)x86p_jit_chain_census_unrecorded(census) /
          (double)entries,
      x86p_jit_chain_census_blocks(census),
      (unsigned long long)x86p_jit_chain_census_dropped_keys(census),
      (unsigned long long)x86p_jit_chain_census_overflowed(census));
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
  uint64_t dropped;
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
  dropped = x86p_jit_profile_dropped_keys(profile);
  /*
   * A DROPPED KEY IS NOT A SHORTER TAIL. The table refuses new keys once it is
   * full, so a block first entered after that point is absent from the
   * histogram however often it runs -- and the list below is then a ranking of
   * whatever was early, not of what is hot. Measured on the API 35 emulator: a
   * wedged run reported 1,761,605,419 entries with 1,761,478,604 dropped, and
   * its top entry had 11,630 hits and 0.0%. The spinning block was not in the
   * list at all.
   *
   * So the list is printed either way -- it is still what the table holds --
   * and the line above it says which of the two it is.
   */
  if (dropped > total - dropped) {
    lucent_log_info(
        "engine",
        "%sJIT hot blocks: NOT A RANKING -- %llu of %llu entrie(s) were "
        "dropped because the %u-slot table was full, so a block first entered "
        "after it filled is absent whatever it costs. Raise jit.profile past "
        "the block count to rank this run. The %u block(s) the table did hold "
        "follow",
        tag, (unsigned long long)dropped, (unsigned long long)total,
        x86p_jit_profile_distinct(profile), count);
  } else {
    lucent_log_info("engine",
                    "%sJIT hot blocks: %u distinct, %llu entries total, %llu "
                    "key(s) dropped, top %u follows",
                    tag, x86p_jit_profile_distinct(profile),
                    (unsigned long long)total, (unsigned long long)dropped,
                    count);
  }
  for (i = 0; i < count; i++) {
    const char *name = x86_native_name_at(top[i].guest_eip);
    lucent_log_info("engine", "%s%2u. 0x%08x %-40s %10llu %5.1f%%", tag, i + 1u,
                    top[i].guest_eip, name ? name : "unnamed",
                    (unsigned long long)top[i].entries,
                    100.0 * (double)top[i].entries / (double)total);
  }
}
