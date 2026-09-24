#include "x86_engine_report.h"

#include "x86_engine_dispatch_report.h"

#include "guest_memory.h"
#include "x86_engine_x87_census.h"
#include "x86rt_native.h"

#include "jit_engine.h"

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

/* Where block dispatch finds its translations: the front cache, the table,
   or neither. A front-hit share far below the hit rate means the run's hot
   set outgrew the front cache, and every block entry pays a probe of the
   whole table instead. Intercept calls near the blocks entered mean the
   intercept contract is not in force and every block pays that call. */
static void report_block_cache(const X86pJitEngineStats *js) {
  /* A refused lookup (a guarded block, or a host thunk the cache remembers
     having none) is followed by the intercept call and, when the block may be
     entered, a second lookup -- so it is not in the hit rate's denominator. */
  const uint64_t answerable = js->cache_lookups - js->cache_refused;
  if (answerable == 0u) {
    lucent_log_info("engine", "[HB] block cache: no lookups yet");
    return;
  }
  lucent_log_info(
      "engine",
      "[HB] block cache: %llu of %llu lookup(s) hit (%.2f%%), %llu answered "
      "by the front cache (%.2f%%), %llu more refused to ask first; intercept "
      "asked %llu time(s) for %llu block(s) entered, %llu translation(s) "
      "guarded",
      (unsigned long long)js->cache_hits, (unsigned long long)answerable,
      100.0 * (double)js->cache_hits / (double)answerable,
      (unsigned long long)js->cache_front_hits,
      100.0 * (double)js->cache_front_hits / (double)answerable,
      (unsigned long long)js->cache_refused,
      (unsigned long long)js->intercept_calls,
      (unsigned long long)js->blocks_entered,
      (unsigned long long)js->blocks_guarded);
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
  lucent_log_info(
      "engine",
      "[HB] the primary engine's last block entry was 0x%08x (%s)."
      " With %.1f%% of entries re-entering the block just left, "
      "that is where this run is looping; --set jit.watch=0x%08x "
      "reports its register file.",
      last, name ? name : "unnamed",
      100.0 * (double)js->blocks_reentered / (double)js->blocks_entered, last);
}

void x86_engine_report_live_if_requested(const X86EngineJitPool *jit,
                                         unsigned long callouts) {
  /* Every engine call passes here, so the stats block is zeroed only once a
     report is actually due: zeroing it first was a memset per call. */
  if (!atomic_load_explicit(&g_live_requested, memory_order_relaxed) ||
      !atomic_exchange_explicit(&g_live_requested, 0, memory_order_relaxed)) {
    return;
  }
  X86pJitEngineStats js = {0};
  if (jit) {
    x86_engine_jit_pool_stats(jit, &js);
  }
  lucent_log_info(
      "engine",
      "[HB] JIT: %llu blocks entered (%llu re-entered the block just left, "
      "%.1f%%; %llu by a chained exit, %.1f%%, over %llu link(s)), "
      "%llu translated (%llu instructions); "
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
      (unsigned long long)js.blocks_chained,
      js.blocks_entered
          ? 100.0 * (double)js.blocks_chained / (double)js.blocks_entered
          : 0.0,
      (unsigned long long)js.chain_links,
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
  report_block_cache(&js);
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
  /* Leaves: direct CALLs translated to call one, and CALLs through a register
     or memory given a site that asks for one at run time. Zero sites with
     leaves installed is a translator that never reached an indirect CALL. */
  lucent_log_info(
      "engine",
      "JIT leaves: %llu direct CALL(s) call a leaf; %llu indirect CALL(s) "
      "have a site (%llu more found the pool spent), whose %llu target "
      "answer(s) named %llu leaf/leaves, %llu site(s) out of answers",
      (unsigned long long)js.leaf_calls, (unsigned long long)js.leaf_sites,
      (unsigned long long)js.leaf_sites_refused,
      (unsigned long long)js.leaf_site_fills,
      (unsigned long long)js.leaf_site_leaf_fills,
      (unsigned long long)js.leaf_sites_exhausted);
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
