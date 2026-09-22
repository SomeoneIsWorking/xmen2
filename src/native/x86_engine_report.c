#include "x86_engine_report.h"

#include "guest_memory.h"
#include "x86_engine_x87_census.h"
#include "x86rt_native.h"

#include "jit_chain_census.h"
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
/*
 * Whether the translated blocks are sharing modules, or the run is holding
 * one engine module per block.
 *
 * The count alone cannot say that: zero compactions reads the same on a
 * backend that emits machine code and has no modules to gather, on a run too
 * short to fill one batch, and on a compactor that is wired in but never
 * fires. So the translated-block total is the denominator, and the three
 * cases are worded apart.
 */
/* The primary engine's last refusal. A pool has one per guest thread and only
   a counter can be summed, so this names the engine it came from rather than
   pretending to speak for all of them. */
static const char *refusal_reason(const X86EngineJitPool *jit) {
  const X86pJitEngine *primary = x86_engine_jit_pool_primary(jit);
  return primary ? x86p_jit_engine_compaction_refusal_reason(primary) : "";
}

static void report_compaction(const X86pJitEngineStats *js, const char *why,
                              const char *prefix) {
  if (js->blocks_translated == 0u) {
    lucent_log_info("engine",
                    "%sJIT modules: no block was translated, so this run says "
                    "nothing about module sharing",
                    prefix);
  } else if (js->compactions == 0u) {
    lucent_log_info("engine",
                    "%sJIT modules: none of the %llu translated block(s) were "
                    "gathered, so this run holds one module per block "
                    "(%llu gathering(s) refused)",
                    prefix, (unsigned long long)js->blocks_translated,
                    (unsigned long long)js->compaction_refusals);
  } else {
    lucent_log_info("engine",
                    "%sJIT modules: %llu translated block(s) were gathered "
                    "into %llu shared module(s), %llu gathering(s) refused; "
                    "%llu block(s) waiting for a batch and %llu engine(s) "
                    "have stopped gathering for good",
                    prefix, (unsigned long long)js->blocks_translated,
                    (unsigned long long)js->compactions,
                    (unsigned long long)js->compaction_refusals,
                    (unsigned long long)js->compaction_pending,
                    (unsigned long long)js->compaction_stopped);
  }
  /* What a refusal SAID, not only that there was one. The gathering that a
     browser run refused is also the one that taught the arena a ceiling that
     did not exist, and its words were counted and then thrown away. */
  if (js->compaction_refusals > 0u && why && why[0]) {
    lucent_log_info("engine", "%s  the last refused gathering: %s", prefix,
                    why);
  }
}

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
                        "small for the working set by exactly that much "
                        "(holding %llu KiB of %llu KiB across %llu block "
                        "record(s), so the caps a run was started with are "
                        "beside what it reached)"
                      : "[HB] the engine evicted: %llu time(s), %llu block(s), "
                        "of %llu translated -- the code arena holds the whole "
                        "working set reached so far (%llu KiB of %llu KiB, "
                        "%llu block record(s))",
                  (unsigned long long)js->evictions,
                  (unsigned long long)js->eviction_blocks_dropped,
                  (unsigned long long)js->blocks_translated,
                  (unsigned long long)(js->code_bytes_used / 1024u),
                  (unsigned long long)(js->code_bytes_limit / 1024u),
                  (unsigned long long)js->block_records);
  if (js->evictions > 0u) {
    lucent_log_info("engine",
                    "[HB]   asked for by: %llu the byte budget, %llu the "
                    "module slots, %llu the live-module ceiling the engine "
                    "has shown",
                    (unsigned long long)js->evictions_out_of_bytes,
                    (unsigned long long)js->evictions_out_of_slots,
                    (unsigned long long)js->evictions_at_engine_limit);
    /* The ceiling is a hypothesis about a limit the engine will not state, so
       a run has to say whether its refusals survived the back-off. Retiring
       as many as it learns is a host that refuses under momentary pressure;
       learning one and keeping it is a limit that really holds. */
    lucent_log_info("engine",
                    "[HB]   the engine's refusals put %llu ceiling(s) in "
                    "force, %llu of which did not survive the back-off",
                    (unsigned long long)js->ceilings_learned,
                    (unsigned long long)js->ceilings_retired);
  }
}

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

/*
 * WHICH block the run is going round on -- the one thing the share above
 * cannot say.
 *
 * A high re-entry share is the signature of both a healthy tight loop and a
 * wedge, and neither the share nor the block-entry histogram names the block:
 * the histogram refuses new keys once its table is full, so a spin that starts
 * after that is absent from it entirely. This is the last address the engine
 * dispatched to, which on a run that has stopped making progress IS the
 * spinning block, and it is the address `jit.watch` takes.
 *
 * It is one sample from the primary engine, so it is printed only when the
 * share says a loop is what the run is doing -- on an ordinary beat it would
 * be a random block dressed up as a finding.
 */
static void report_last_block_entry(const X86EngineJitPool *jit,
                                    const X86pJitEngineStats *js) {
  uint32_t last;
  const char *name;
  if (!jit || js->blocks_entered == 0u ||
      js->blocks_reentered * 2u < js->blocks_entered) {
    return;
  }
  last = x86p_jit_engine_last_block_entry(x86_engine_jit_pool_primary(jit));
  name = x86_native_name_at(last);
  lucent_log_info("engine",
                  "[HB] the primary engine's last block entry was 0x%08x (%s)."
                  " With %.1f%% of entries re-entering the block just left, "
                  "that is where this run is looping; --set jit.watch=0x%08x "
                  "reports its register file.",
                  last, name ? name : "unnamed",
                  100.0 * (double)js->blocks_reentered /
                      (double)js->blocks_entered,
                  last);
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
      "[HB] JIT: %llu blocks entered (%llu re-entered the block just left, "
      "%.1f%%), %llu translated (%llu instructions); "
      "%lu native hand-backs; %llu refusals of %llu translation attempts; "
      "%llu cache flushes, %llu bytes code; %llu of %llu condition(s) "
      "lowered inline (%llu unrecorded predecessor, %llu underivable kind); "
      "%llu exit(s), %llu with a known successor, %llu backward, %llu to the "
      "block's own entry; %llu of %llu x87 load(s) widened in the block; "
      "%llu of %llu SIMD instruction(s) emitted as host SIMD; "
      "%llu of %llu x87 store(s) narrowed in the block; "
      "product fallback unavailable",
      (unsigned long long)js.blocks_entered,
      (unsigned long long)js.blocks_reentered,
      js.blocks_entered
          ? 100.0 * (double)js.blocks_reentered / (double)js.blocks_entered
          : 0.0,
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
                           js.conds_unknown_kind),
      (unsigned long long)js.exits, (unsigned long long)js.exits_static,
      (unsigned long long)js.exits_backward, (unsigned long long)js.exits_self,
      (unsigned long long)js.x87_loads_inline,
      (unsigned long long)js.x87_loads_translated,
      (unsigned long long)js.simd_inline,
      (unsigned long long)js.simd_translated,
      (unsigned long long)js.x87_stores_inline,
      (unsigned long long)js.x87_stores_translated);
  report_last_block_entry(jit, &js);
  report_compaction(&js, refusal_reason(jit), "[HB] ");
  report_invalidation(&js);
  x86_engine_report_chain_census(jit, "[HB] ");
  x86_engine_x87_census_report("[HB] ");
  x86_engine_report_hot_blocks(jit, "[HB] ");
}

/*
 * Everything the counters say once the run is over.
 *
 * It lives here rather than in the engine for the reason the live heartbeat
 * does: the engine runs guest code, and deciding what a pile of counters has
 * to SAY -- which denominator each share needs, and what a zero in it means --
 * is a different job, with a different reason to change.
 */
void x86_engine_report_jit_totals(const X86EngineJitPool *jit) {
  X86pJitEngineStats js;
  if (!jit) {
    return;
  }
  x86_engine_jit_pool_stats(jit, &js);
  lucent_log_info(
      "engine",
      "JIT: %llu block(s) entered (%llu translated, %llu instructions), "
      "%llu refusal(s), %llu flush(es), %zu KiB code (%s)",
      (unsigned long long)js.blocks_entered,
      (unsigned long long)js.blocks_translated,
      (unsigned long long)js.guest_insns_translated,
      (unsigned long long)js.translate_refusals,
      (unsigned long long)js.cache_flushes, (size_t)(js.code_bytes_used / 1024),
      x86p_jit_engine_mechanism());
  /* How much of the translated code reads its Jcc/SETcc conditions off the
     host's own flags. Reported with the total, because a backend that lowers
     none is correct and merely pays a call per condition -- a bare inline
     count could not be told from a run that translated no branches. */
  if (js.conds_translated == 0u)
    lucent_log_info("engine",
                    "JIT conditions: none translated, so this run says "
                    "nothing about condition lowering");
  else
    lucent_log_info(
        "engine",
        "JIT conditions: %llu of %llu lowered inline (%.1f%%), %llu call "
        "the shared evaluator (%llu of those had no recorded predecessor, "
        "%llu had one no derivation covers)",
        (unsigned long long)js.conds_inline,
        (unsigned long long)js.conds_translated,
        100.0 * (double)js.conds_inline / (double)js.conds_translated,
        (unsigned long long)(js.conds_translated - js.conds_inline),
        (unsigned long long)js.conds_unknown_kind,
        (unsigned long long)(js.conds_translated - js.conds_inline -
                             js.conds_unknown_kind));
  /* The same shape of negative, for the same reason: only the WebAssembly
     backend fills these, and a row of zeros would read as a run whose blocks
     had nowhere to go rather than as a backend that does not count. */
  if (js.exits == 0u)
    lucent_log_info("engine",
                    "JIT exits: none counted, so this build's backend does "
                    "not report where its blocks go");
  else
    lucent_log_info(
        "engine",
        "JIT exits: %llu from %llu block(s) (%.2f each); %llu have a "
        "successor the translator already knows (%.1f%%), of which %llu are "
        "backward -- a guest loop paying a dispatch per iteration (%.1f%%) "
        "-- and %llu name the block's own entry (%.1f%%)",
        (unsigned long long)js.exits, (unsigned long long)js.blocks_translated,
        js.blocks_translated ? (double)js.exits / (double)js.blocks_translated
                             : 0.0,
        (unsigned long long)js.exits_static,
        100.0 * (double)js.exits_static / (double)js.exits,
        (unsigned long long)js.exits_backward,
        100.0 * (double)js.exits_backward / (double)js.exits,
        (unsigned long long)js.exits_self,
        100.0 * (double)js.exits_self / (double)js.exits);
  /* FLD m32/m64, and how much of it the emitted code widens itself instead
     of crossing out of its module. Same negative as the two above: a zero
     inline count on a nonzero total is a backend that declined, and a zero
     total is a corpus with no float loads in it. */
  if (js.x87_loads_translated == 0u)
    lucent_log_info("engine",
                    "JIT x87 loads: none translated, so this run says "
                    "nothing about the inline widening");
  else
    lucent_log_info(
        "engine",
        "JIT x87 loads: %llu of %llu widened in the block (%.1f%%), %llu "
        "call out of the module",
        (unsigned long long)js.x87_loads_inline,
        (unsigned long long)js.x87_loads_translated,
        100.0 * (double)js.x87_loads_inline / (double)js.x87_loads_translated,
        (unsigned long long)(js.x87_loads_translated - js.x87_loads_inline));
  /* FST m32/m64, and how much of it the emitted code narrows itself. The
     share here is a property of the VALUES the run stored as well as of the
     code, because the inline arm rounds and declines what it cannot. */
  if (js.x87_stores_translated == 0u)
    lucent_log_info("engine",
                    "JIT x87 stores: none translated, so this run says "
                    "nothing about the inline narrowing");
  else
    lucent_log_info(
        "engine",
        "JIT x87 stores: %llu of %llu narrowed in the block (%.1f%%), %llu "
        "call out of the module",
        (unsigned long long)js.x87_stores_inline,
        (unsigned long long)js.x87_stores_translated,
        100.0 * (double)js.x87_stores_inline / (double)js.x87_stores_translated,
        (unsigned long long)(js.x87_stores_translated - js.x87_stores_inline));
  /* The packed SSE forms, and how much of it the emitted code performs with
     the host's own 128-bit SIMD instead of a lane-at-a-time C helper across
     the module boundary. Same negative as the rows above. */
  if (js.simd_translated == 0u)
    lucent_log_info("engine",
                    "JIT SIMD: none translated, so this run says nothing "
                    "about the host-SIMD lowering");
  else
    lucent_log_info(
        "engine",
        "JIT SIMD: %llu of %llu emitted as host SIMD (%.1f%%), %llu call "
        "out of the module",
        (unsigned long long)js.simd_inline,
        (unsigned long long)js.simd_translated,
        100.0 * (double)js.simd_inline / (double)js.simd_translated,
        (unsigned long long)(js.simd_translated - js.simd_inline));
  report_compaction(&js, refusal_reason(jit), "");
  x86_engine_report_chain_census(jit, "");
  x86_engine_x87_census_report("");
  x86_engine_report_hot_blocks(jit, "");
}
