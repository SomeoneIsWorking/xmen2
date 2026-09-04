#include "../config/environment.h"
#include "x2_log.h"
/*
 * The title's native runtime: dispatch across every mapped guest module.
 * See x86rt_native.h for why dispatch keys on the mapped address rather than
 * the guest entry point.
 */
#include "guest_heap.h"
#include "guest_memory.h"
#include "host_imports.h"
#include "pe_map.h"
#include "threads.h"
#include "x86_dispatch_report.h"
#include "x86_engine.h"
#include "x86_hotep.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <sys/syscall.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

static X86Module *g_head;

/* Native overrides: (module, linked entry point) -> C implementation.
   Registered from the subsystem files by x86_register_override; the dispatcher
   checks this table before ordinary guest execution. Declarations live in
   the source file that owns the override.

   The key is a module NAME plus the entry point at that module's PREFERRED
   base, never a bare address. Every libIG*.dll is linked for 0x10000000, so a
   bare entry point names up to eight different functions -- 0x10002520 is a
   real function in eight of this game's modules -- while dispatch works in
   MAPPED addresses, which are unique. A table keyed on the linked address
   therefore both misses its intended target (the module was relocated) and
   answers for whichever unrelated module happened to keep the preferred base.
   That is the same collision this file's header describes for the function
   tables, and it is why registration is resolved against a named module. */
#define X2_MAX_OVERRIDES 128
static struct {
  const char *module; /* module name as pe_map/x86_module_register knows it */
  uint32_t linked_ep; /* entry point at that module's PREFERRED base */
  uint32_t mapped_ep; /* where it actually landed; valid once resolved */
  x86_override_fn fn;
} g_override[X2_MAX_OVERRIDES];
static int g_noverride;
static int g_overrides_resolved;

/*
 * The set of mapped addresses this dispatcher owns, as a hash set.
 *
 * The execution engine asks "is there host code here?" at EVERY guest
 * instruction, so the answer has to be a probe rather than a walk. The thunk
 * range answers itself (it is contiguous); this holds the override entry
 * points, which are not.
 *
 * Zero means empty, which is safe because 0 is never a mapped entry point --
 * an image mapped at 0 is refused by pe_map long before this. Sized a power of
 * two four times X2_MAX_OVERRIDES so the load factor stays low enough that a
 * miss is one or two probes; a full table would loop forever, so insertion
 * refuses rather than trusting that it cannot fill.
 */
#define OWNED_SLOTS (X2_MAX_OVERRIDES * 4)
static uint32_t g_owned[OWNED_SLOTS];
X2_INTERNAL uint32_t g_override_bloom[64];

static unsigned owned_slot(uint32_t addr) {
  /* Entry points are 4- or 16-byte aligned, so the low bits carry little;
     Fibonacci hashing spreads the useful ones over the table. */
  return (unsigned)(((addr * 2654435761u) >> 8) & (OWNED_SLOTS - 1u));
}

static void owned_insert(uint32_t addr) {
  unsigned i = owned_slot(addr), n = 0;
  while (g_owned[i]) {
    if (g_owned[i] == addr)
      return;
    i = (i + 1u) & (OWNED_SLOTS - 1u);
    if (++n == OWNED_SLOTS) {
      x2_log_error("x86rt: owned-address set full at %d, addr 0x%08x\n",
                   OWNED_SLOTS, addr);
      abort();
    }
  }
  g_owned[i] = addr;
  x86_override_bloom_add(addr);
}

static int owned_has(uint32_t addr) {
  unsigned i = owned_slot(addr);
  for (;;) {
    uint32_t v = g_owned[i];
    if (!v)
      return 0;
    if (v == addr)
      return 1;
    i = (i + 1u) & (OWNED_SLOTS - 1u);
  }
}

void x86_register_override(const char *module, uint32_t linked_ep,
                           x86_override_fn fn) {
  int i;
  if (!module || !*module) {
    x2_log_error("x86_register_override: 0x%08x registered with no "
                 "module name. An override is only meaningful against "
                 "the module that owns the entry point.\n",
                 linked_ep);
    abort();
  }
  for (i = 0; i < g_noverride; i++) {
    if (g_override[i].linked_ep == linked_ep &&
        !strcmp(g_override[i].module, module)) {
      x2_log_error("x86_register_override: %s 0x%08x registered "
                   "TWICE; the new function replaces the old. An "
                   "override declared in two files is a defect -- "
                   "naming it here.\n",
                   module, linked_ep);
      g_override[i].fn = fn;
      return;
    }
  }
  if (g_noverride >= X2_MAX_OVERRIDES) {
    x2_log_error("x86_register_override: the table holds %d and is "
                 "full; %s 0x%08x is NOT registered. Raise "
                 "X2_MAX_OVERRIDES rather than letting an override "
                 "silently not fire.\n",
                 X2_MAX_OVERRIDES, module, linked_ep);
    abort();
  }
  g_override[g_noverride].module = module;
  g_override[g_noverride].linked_ep = linked_ep;
  g_override[g_noverride].mapped_ep = 0;
  g_override[g_noverride].fn = fn;
  g_noverride++;
  /* Registered after the resolve pass -- an ARK class substituted from a
     trigger, for instance -- resolves now. Leaving mapped_ep at 0 would let
     the override sit in the table and never fire, which is the failure
     x86_overrides_resolve aborts to prevent. */
  if (g_overrides_resolved) {
    char why[256];
    uint32_t mapped = 0;
    if (x86_override_resolve_check(module, linked_ep, &mapped, why,
                                   sizeof why) != 0) {
      x2_log_error("x86_register_override: %s 0x%08x is registered "
                   "after the resolve pass and cannot be resolved "
                   "now: %s\n",
                   module, linked_ep, why);
      abort();
    }
    g_override[g_noverride - 1].mapped_ep = mapped;
    owned_insert(mapped);
  }
}

int x86_override_count(void) { return g_noverride; }

int x86_override_is_bound(const char *module, uint32_t linked_ep,
                          x86_override_fn fn) {
  int i;
  if (!g_overrides_resolved)
    return 0;
  for (i = 0; i < g_noverride; i++)
    if (g_override[i].mapped_ep && g_override[i].linked_ep == linked_ep &&
        g_override[i].fn == fn && !strcmp(g_override[i].module, module))
      return 1;
  return 0;
}

/* Resolve ONE (module, linked ep) to the mapped address dispatch will compare.
   Returns 0 and fills *mapped_out on success; non-zero with a reason in `why`
   when the pair could not be resolved. Split out from the loop below so the
   rejection paths can be exercised by --override-selftest: a resolver that
   aborts on every failure cannot be shown to accept the right things and
   reject the wrong ones without a way to ask it. */
uint32_t x86_module_base(const char *image) {
  X86Module *m;
  for (m = g_head; m; m = m->next)
    if (!strcasecmp(m->name, image))
      return *m->base;
  return 0;
}

int x86_override_resolve_check(const char *module, uint32_t linked_ep,
                               uint32_t *mapped_out, char *why, size_t whyn) {
  X86Module *m;
  uint32_t mapped;
  for (m = g_head; m; m = m->next)
    if (!strcmp(m->name, module))
      break;
  if (!m) {
    snprintf(why, whyn,
             "module %s is NOT mapped -- either the name is "
             "wrong or it was not linked into this build",
             module);
    return 1;
  }
  if (linked_ep < m->preferred || linked_ep >= m->preferred + m->size) {
    snprintf(why, whyn, "0x%08x is outside %s's image (0x%08x + 0x%x)",
             linked_ep, module, m->preferred, m->size);
    return 1;
  }
  mapped = *m->base + (linked_ep - m->preferred);
  /* Runtime decoding accepts any valid instruction boundary. The registered
     address is therefore mapped exactly; an invalid or mid-instruction entry
     fails in x86port instead of being guessed at registration time. */
  *mapped_out = mapped;
  return 0;
}

/* Turn every (module, linked ep) into the mapped address dispatch will see.
   Called once, after every module has registered and been mapped: the
   registrations run from constructors, which is before pe_map has placed
   anything, so the mapped address cannot be known at registration time.

   Every failure here is fatal by design. An override that does not resolve is
   invisible at runtime -- the game runs, the native code simply never executes
   and ordinary guest execution answers instead -- which is indistinguishable
   from a working build until something downstream is wrong for reasons that
   look unrelated. */
/*
 * The mapped entry point of a resolved override, by index, or 0.
 *
 * Exists so a caller can obtain an address that x86_native_body_at MUST answer
 * yes for. A predicate that has only ever been asked about addresses it says no
 * to has not been tested -- it is indistinguishable from `return 0`.
 */
uint32_t x86_override_mapped_ep(int index) {
  if (index < 0 || index >= g_noverride)
    return 0;
  return g_override[index].mapped_ep;
}

void x86_overrides_resolve(void) {
  int i, bad = 0;
  for (i = 0; i < g_noverride; i++) {
    char why[256];
    uint32_t mapped = 0;
    if (x86_override_resolve_check(g_override[i].module,
                                   g_override[i].linked_ep, &mapped, why,
                                   sizeof why) != 0) {
      x2_log_error("x86_overrides_resolve: override for %s 0x%08x "
                   "could not be resolved: %s\n",
                   g_override[i].module, g_override[i].linked_ep, why);
      bad++;
      continue;
    }
    g_override[i].mapped_ep = mapped;
    owned_insert(mapped);
  }
  if (bad) {
    x2_log_error("x86_overrides_resolve: %d of %d override(s) could not "
                 "be resolved. Refusing to run: a silently absent "
                 "override looks exactly like a working build.\n",
                 bad, g_noverride);
    abort();
  }
  g_overrides_resolved = 1;
  /* Bound to the mapped guest address the dispatcher compares. It is the
     honest key: x86_dispatch_one holds it while deciding whether a registered
     native override or the JIT answers. */
  x2_log_info("overrides: %d native override(s) bound to a mapped address\n",
              g_noverride);
}

static int thunk_call(uint32_t addr, CPU *C);
static FILE *g_sc_out;
static int g_sc_armed;
static unsigned long g_sc_records;
extern volatile sig_atomic_t
    x2_report_now; /* heartbeat: set when the run stops */

/* The current guest body, written on every dispatch (guest body or import
   stub), read by the X2_PROFILE sampler thread. Declared here, at the top,
   because the two dispatch paths that write it precede the profiler block. */
volatile uint32_t g_sample_ep;

/* X2_GUEST_WATCH diagnostic: which body ran right after a guest address went
   to zero. Set once from the environment; see the dispatch path. */
uint32_t g_guest_watch_addr;
static uint32_t g_last_dispatch_ep;

/* X2_WRITE_WATCH=<guest-addr>[:<value>]: armed by x86_write_watch_arm;
   WR8/16/32 call x2_write_watch_fire the moment the watched guest address is
   written.

   It does NOT stop at the first hit. A stack address is reused by every frame
   that passes through it, so on a guest stack the first write is almost never
   the interesting one: watching a /GS cookie slot, the single shot was spent
   on an unrelated frame's write hundreds of frames before the overrun, and the
   watch then sat disarmed through the corruption it was armed for and reported
   nothing. A one-shot watch on a hot address reports the wrong writer and
   looks like an answer.

   So every write is reported, and the optional :<value> filter is what narrows
   a hot slot to the interesting case ("who writes ZERO here") instead of
   narrowing it to "whoever got here first". */
volatile uint32_t x2_write_watch_addr;
static int g_ww_filter;         /* a :<value> filter was given */
static uint32_t g_ww_value;     /* ... and the value it selects */
static unsigned long g_ww_hits; /* writes seen, filter included */
static unsigned long g_ww_reported;

/* Cap the BORING case only: the first few writes, plus EVERY write of the
   filtered value. A cap that hides the interesting write is how a watch
   reports nothing and reads as "nothing happened". */
#define WW_REPORT_FIRST 8

void x2_write_watch_fire(uint32_t a, uint32_t v) {
  extern const char *x86_native_name_at(uint32_t);
  const char *nm;
  X86Module *m;

  g_ww_hits++;
  if (g_ww_filter && v != g_ww_value)
    return;
  /* Unfiltered: the first few, plus the two writes that MATTER on a /GS
     cookie slot -- the store of the process cookie (the frame arming its
     tripwire) and any store of zero (the tripwire being wiped). Reporting
     only zeros shows the wipes but not which frame's cookie was wiped, so
     the pair is what makes the sequence readable. */
  if (!g_ww_filter && g_ww_reported >= WW_REPORT_FIRST && v != 0 &&
      v != RD32(0x006f38f8))
    return;

  nm = x86_native_name_at(g_sample_ep);
  m = x86_module_for(g_sample_ep);
  g_ww_reported++;
  /* g_sample_ep is the last DISPATCHED body, which is the writer only when
     the writer was reached through the dispatcher. Reached by a direct C
     call it names an ANCESTOR -- narrowing, not naming, and it says so. */
  x2_log_error("[WWATCH] write #%lu to 0x%08x = 0x%08x (process cookie "
               "0x%08x); last dispatched body 0x%08x %s%s%s%s\n",
               g_ww_hits, a, v, RD32(0x006f38f8), g_sample_ep, nm ? "" : "in ",
               nm ? nm : (m ? m->name : "???"), (nm || !m) ? "" : " +offset",
               v == 0
                   ? "   <-- ZERO"
                   : (v == RD32(0x006f38f8) ? "   <-- /GS cookie stored" : ""));
}

/* How many writes the watch saw, so a run can report a real denominator --
   "0 of 0" and "0 of 12,043" are different answers. */
unsigned long x86_write_watch_hits(void) { return g_ww_hits; }

void x86_write_watch_arm(const char *arg) {
  const char *colon;
  if (!arg || !*arg)
    return;
  x2_write_watch_addr = (uint32_t)strtoul(arg, NULL, 0);
  colon = strchr(arg, ':');
  if (colon) {
    g_ww_filter = 1;
    g_ww_value = (uint32_t)strtoul(colon + 1, NULL, 0);
  }
  if (!x2_write_watch_addr) {
    x2_log_error("X2_WRITE_WATCH=%s parsed to address 0; the watch is "
                 "NOT armed and nothing will be reported.\n",
                 arg);
    return;
  }
  if (g_ww_filter)
    x2_log_error("X2_WRITE_WATCH=0x%08x:0x%08x: every guest write of "
                 "that value to that address is reported, all of them.\n",
                 x2_write_watch_addr, g_ww_value);
  else
    x2_log_error("X2_WRITE_WATCH=0x%08x: the first %d guest write(s) to "
                 "this address are reported, plus every /GS cookie store "
                 "and every write of ZERO.\n",
                 x2_write_watch_addr, WW_REPORT_FIRST);
}
static void ring_note(const char *what, uint32_t addr, uint32_t base,
                      uint32_t in, uint32_t out, uint32_t ret);
extern const CPU *g_cpu_current;

/* Cumulative EXCLUSIVE ns inside host import stubs and inside guest bodies
   since arming, for x86_probe_time_delta. Read with the same torn-read trade
   as the crossing counter.
 *
   "Exclusive" matters: a dispatched body runs nested dispatches and imports
   inside its own span, so an inclusive span counter would charge the same
   wall time at every nesting level (a 5s interval once measured 167s of
   "guest" time on a single-threaded scheduler -- impossible). The span stack
   below charges each level only its own compute: a level's span minus the
   spans of every child level pushed on top of it. Direct guest-to-guest
   calls never dispatch, so they remain inside the enclosing body's exclusive
   span, which is exactly the attribution wanted for naming a hot body. */
static unsigned long long g_host_import_ns, g_guest_body_ns;

static inline unsigned long long probe_ns_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

#define SPAN_STACK_MAX 64
static unsigned long long g_span_start[SPAN_STACK_MAX];
static unsigned long long g_span_child[SPAN_STACK_MAX];
static int g_span_depth;

static inline void span_push(void) {
  if (g_span_depth < SPAN_STACK_MAX) {
    g_span_start[g_span_depth] = probe_ns_now();
    g_span_child[g_span_depth] = 0;
  }
  g_span_depth++;
}

/* Returns this level's exclusive ns and charges its full span to the parent
   level so the parent's own exclusive time excludes it. */
static inline unsigned long long span_pop(void) {
  unsigned long long full, excl;
  if (g_span_depth <= 0)
    return 0;
  g_span_depth--;
  if (g_span_depth >= SPAN_STACK_MAX)
    return 0; /* slot overflowed, lost */
  full = probe_ns_now() - g_span_start[g_span_depth];
  excl = full - g_span_child[g_span_depth];
  if (g_span_depth > 0 && g_span_depth - 1 < SPAN_STACK_MAX)
    g_span_child[g_span_depth - 1] += full;
  return excl;
}

/* The main executable's mapped base and bounds, retained for title-owned
   address helpers and diagnostics. Modules otherwise own their own bases. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;

void x86_module_register(X86Module *m) {
  m->next = g_head;
  g_head = m;
}

X86Module *x86_modules(void) { return g_head; }

X86Module *x86_module_for(uint32_t addr) {
  X86Module *m;
  for (m = g_head; m; m = m->next) {
    uint32_t b = *m->base;
    /* size 0 means the host never mapped this module. Saying so beats
       returning NULL, which reads as "that address is host memory". */
    if (b && !m->size) {
      x2_log_error("x86_module_for: %s has a base but no size -- the "
                   "host mapped it and did not record how big it is, "
                   "so every lookup into it will miss\n",
                   m->name);
      abort();
    }
    if (b && addr >= b && addr - b < m->size)
      return m;
  }
  return NULL;
}

/*
 * One-shot triggers: run host code the first time the guest calls a given
 * address.
 *
 * Substituting an engine class cannot happen at module-init time -- ARK
 * registration goes through igGetMemoryPool, and the pools do not exist until
 * the exe's engine startup has run, so registering early faults on a NULL pool
 * inside libIGCore. Nor can it happen after --run returns, by which point the
 * engine has torn down. It has to happen at a MOMENT DURING the run, and the
 * only reliable way to name that moment is an address the engine itself
 * reaches once it is ready.
 *
 * Deliberately fires BEFORE the body, and deliberately once: a trigger that
 * refires would re-register a class on every call.
 */
#define MAX_TRIG 8
static struct {
  uint32_t addr;
  int (*fn)(void);
  const char *why;
  int fired, active;
} g_trig[MAX_TRIG];
static int g_ntrig;

void x86_at_first_call(uint32_t addr, int (*fn)(void), const char *why) {
  if (g_ntrig == MAX_TRIG) {
    x2_log_error("x86_at_first_call: no room for a trigger on 0x%08x\n", addr);
    abort();
  }
  g_trig[g_ntrig].addr = addr;
  g_trig[g_ntrig].fn = fn;
  g_trig[g_ntrig].why = why;
  g_trig[g_ntrig].fired = 0;
  g_ntrig++;
}

/* Whether every armed trigger actually fired. A trigger that never fires is
   the failure mode that looks like success: the run completes, nothing was
   substituted, and nothing said so. */
int x86_triggers_report(void) {
  int i, unfired = 0;
  for (i = 0; i < g_ntrig; i++)
    if (!g_trig[i].fired) {
      x2_log_error("trigger NEVER FIRED: 0x%08x (%s) -- the guest "
                   "never called that address, so the host code "
                   "waiting on it did not run.\n",
                   g_trig[i].addr, g_trig[i].why);
      unfired++;
    }
  return unfired;
}

/*
 * X2_EPCOUNT=0x004a11c0,0x004a1320 -- how often a body is ENTERED, in an
 * ordinary build.
 *
 * It costs one comparison per DISPATCHED call, which is the only way a body
 * with no direct call site can be reached at all -- and the two script-launch
 * functions in XMen2.exe have exactly zero direct call sites, so every entry
 * they get comes through here.
 *
 * It prints its counts AT ZERO and with the run's dispatch total, because
 * "this function was never entered" and "the counter never ran" are the two
 * answers that must not look alike -- which is the whole reason the previous
 * attempt at this question was worthless.
 */
static int args_string_at(uint32_t a, char *out, size_t cap);

#define EPCOUNT_MAX 8
#define EPCOUNT_WORDS 64
static struct {
  uint32_t ep;
  unsigned long n;
  /* The RAW argument words, distinct, first-seen order -- and nothing is
   * dereferenced here. The first attempt at this decoded them as strings ON
   * THE DISPATCH PATH and the run died at 12 s with the guest executing a
   * string (I048); reading the words is passive, and turning them into names
   * can wait until the report, when nothing is mid-call. */
  int nwords, lost;
  uint32_t word[EPCOUNT_WORDS];
  /* Distinct RETURN ADDRESSES: which code calls this. The word at ESP on
   * entry is the return address every guest CALL pushes, so reading
   * it is as passive as reading an argument -- and it is what turns "35
   * launches" into "launched from here". */
  int nrets, retlost;
  uint32_t ret[EPCOUNT_WORDS];
} g_epc[EPCOUNT_MAX];
static int g_epc_n = -1;
static unsigned long g_epc_dispatches;

static void epcount_init(void) {
  const char *e = x2_config_override_get(kX2ConfigEpCount);
  char buf[256], *p, *save;
  g_epc_n = 0;
  if (!e || !*e)
    return;
  snprintf(buf, sizeof buf, "%s", e);
  for (p = strtok_r(buf, ",", &save); p && g_epc_n < EPCOUNT_MAX;
       p = strtok_r(NULL, ",", &save))
    g_epc[g_epc_n++].ep = (uint32_t)strtoul(p, NULL, 0);
  x2_log_error("[EPC] counting entries to %d entry point(s) at the "
               "dispatcher. A body with a DIRECT caller is a plain C call "
               "and is invisible here; these are counted only when "
               "dispatched.\n",
               g_epc_n);
}

void x86_epcount_report(void) {
  int i;
  if (g_epc_n < 0)
    epcount_init();
  if (!g_epc_n)
    return;
  x2_log_error("[EPC] %lu dispatched call(s) in this run:\n", g_epc_dispatches);
  for (i = 0; i < g_epc_n; i++) {
    int k, shown = 0;
    x2_log_error("[EPC]   0x%08x  %lu entr%s\n", g_epc[i].ep, g_epc[i].n,
                 g_epc[i].n == 1 ? "y" : "ies");
    if (!g_epc[i].n)
      continue;
    /* Decoded HERE, with the guest stopped, not on the dispatch path. */
    for (k = 0; k < g_epc[i].nwords; k++) {
      char buf[64];
      if (!args_string_at(g_epc[i].word[k], buf, sizeof buf))
        continue;
      x2_log_error("[EPC]       0x%08x -> \"%s\"\n", g_epc[i].word[k], buf);
      shown++;
    }
    x2_log_error("[EPC]       %d of %d distinct argument word(s) "
                 "decoded as text%s\n",
                 shown, g_epc[i].nwords,
                 g_epc[i].lost ? " (and some were dropped: the table is full)"
                               : "");
    for (k = 0; k < g_epc[i].nrets; k++) {
      const char *nm = x86_native_name_at(g_epc[i].ret[k]);
      X86Module *rm = x86_module_for(g_epc[i].ret[k]);
      x2_log_error("[EPC]       called from 0x%08x%s%s%s\n", g_epc[i].ret[k],
                   nm ? " -- " : (rm ? " -- in " : ""),
                   nm ? nm : (rm ? rm->name : ""),
                   (!nm && rm) ? ", not at a named body" : "");
    }
    if (g_epc[i].retlost)
      x2_log_error("[EPC]       ... and %d more distinct call site(s) "
                   "past the table.\n",
                   g_epc[i].retlost);
  }
}

/*
 * X2_STACKCHECK=<file>: record the ESP DELTA of every dispatched call while
 * armed, for tools/stackcheck.py to check against what each guest function's
 * own RET says it pops.
 *
 * A guest call must return with esp raised by 4 (the return address) plus the
 * callee's RET immediate. Nothing in this runtime has ever checked that: the
 * ring records esp_in and esp_out and prints the delta, but a delta is only
 * wrong relative to an expectation, and the expectation lives in the guest
 * binary, not here. So the runtime records and the checker -- which can read
 * the module JSON -- decides.
 *
 * The failure this exists for: FUN_0046b750 stores its /GS cookie at
 * entry_esp-4 and its epilogue reads [ESP+0x20], and those were FOUR BYTES
 * APART, so the epilogue compared a slot that was never the cookie and the
 * /GS check reported a buffer overrun that had not happened. One dword of
 * drift inside the call tree presents as memory corruption somewhere else.
 */
void x86_stackcheck_arm(int on) {
  if (on && !g_sc_out) {
    const char *path = x2_config_override_get(kX2ConfigStackCheck);
    if (!path || !*path)
      return; /* not asked for */
    g_sc_out = fopen(path, "w");
    if (!g_sc_out) {
      x2_log_error("X2_STACKCHECK=%s could not be opened for "
                   "writing; NOTHING will be recorded.\n",
                   path);
      return;
    }
    x2_log_error("X2_STACKCHECK=%s: recording the esp delta of every "
                 "dispatched call while armed.\n",
                 path);
  }
  g_sc_armed = on;
  if (!on && g_sc_out) {
    fflush(g_sc_out);
    x2_log_error("X2_STACKCHECK: %lu dispatched call(s) recorded. A "
                 "count of 0 means the armed window contained no "
                 "dispatched call, NOT that every delta was right.\n",
                 g_sc_records);
  }
}

/* Records the LINKED ep and the module name, not the mapped address. Keyed on
   the mapped address, every relocated DLL missed the checker's expectation
   table and 86%% of a run came back "no known RET" -- unchecked calls counted
   as clean. The module knows where it was placed, so it converts here. */
static void stackcheck_note(X86Module *m, uint32_t ep, uint32_t in,
                            uint32_t out) {
  if (!g_sc_armed || !g_sc_out)
    return;
  g_sc_records++;
  fprintf(g_sc_out, "%s %08x %08x %08x\n", m->name,
          m->preferred + (ep - *m->base), in, out);
}

int x86_native_call_at(uint32_t addr, CPU *C) {
  X86Module *m;
  if (g_ntrig) {
    int i;
    for (i = 0; i < g_ntrig; i++)
      if (!g_trig[i].fired && !g_trig[i].active && g_trig[i].addr == addr) {
        /*
         * RETRIED, not one-shot. "The engine is ready" is a state, not
         * a call site: arming on the first createInstance fired before
         * libIGGfx had registered igVisualContext, so the substitution
         * found a NULL meta and correctly declined. The handler decides
         * when it is ready and returns non-zero to disarm.
         *
         * `active` guards re-entry -- the handler calls guest code that
         * itself reaches createInstance.
         */
        g_trig[i].active = 1;
        if (g_trig[i].fn())
          g_trig[i].fired = 1;
        g_trig[i].active = 0;
      }
  }
  if (thunk_call(addr, C))
    return 1;
  /* A native override shadows ordinary guest execution. Checked before module
     lookup so the override path skips the find() scan and the epcount/ring
     machinery -- the frame-cap override runs every frame, and routing it
     through the full dispatch bookkeeping would be the cost of a diagnostic
     on a hot path. */
  {
    int i;
    if (g_noverride && !g_overrides_resolved) {
      x2_log_error("x86_native_call_at: guest code is running before "
                   "x86_overrides_resolve(); %d override(s) would be "
                   "silently skipped.\n",
                   g_noverride);
      abort();
    }
    for (i = 0; i < g_noverride; i++)
      if (g_override[i].mapped_ep == addr) {
        uint32_t in = C->reg[kX86pEsp];
        g_override[i].fn(C);
        /* Overrides are checked TOO. A hand-written override has to
           emulate the guest RET itself -- pop the return address and
           whatever the callee pops -- and getting that wrong shifts
           the guest stack by a word, which surfaces later as memory
           corruption somewhere unrelated. Returning before this point
           made the 19 overrides the one thing the stack check could
           not see, which is the wrong place to have a blind spot. */
        {
          X86Module *om = x86_module_for(addr);
          if (om)
            stackcheck_note(om, addr, in, C->reg[kX86pEsp]);
        }
        return 1;
      }
  }
  return 0;
}

/*
 * The CPU state at a fault.
 *
 * A guest-to-guest call is a direct C call passing the SAME CPU pointer down,
 * so the pointer recorded at the last boundary crossing is still the live
 * register file however deep the guest has gone since. Without it a fault
 * report can name the instruction and not one operand -- which is how
 * "MOV EDI,[EAX] faulted at 0" and "but EAX was dereferenced fine two
 * instructions earlier" sat as a contradiction with no way to settle it.
 */
const CPU *g_cpu_current;

void x86_regs_dump(void) {
  const CPU *C = g_cpu_current;
  X86Module *m;
  const char *name;
  if (!C) {
    x2_log_error("[REGS] no CPU has crossed the host boundary yet, so "
                 "there is no register file to show.\n");
    return;
  }
  x2_log_error("[REGS] eax %08x  ecx %08x  edx %08x  ebx %08x\n"
               "[REGS] esp %08x  ebp %08x  esi %08x  edi %08x\n",
               C->reg[kX86pEax], C->reg[kX86pEcx], C->reg[kX86pEdx],
               C->reg[kX86pEbx], C->reg[kX86pEsp], C->reg[kX86pEbp],
               C->reg[kX86pEsi], C->reg[kX86pEdi]);
  m = x86_module_for(g_sample_ep);
  name = x86_native_name_at(g_sample_ep);
  x2_log_error("[REGS] current dispatched body 0x%08x%s%s%s%s\n", g_sample_ep,
               m && m->name ? " in " : "", m && m->name ? m->name : "",
               name ? ": " : "", name ? name : "");
  x2_log_error("[REGS] (the register file of the last body to cross the "
               "boundary; guest-to-guest calls share it, so these are "
               "live -- but a body that saved a register to its own C "
               "locals is not reflected here)\n");
}

/*
 * The entry point of the function CONTAINING an address -- the greatest
 * EXPORTED entry point at or below it, within the same mapped image.
 *
 * Exists so that host code can identify its own caller by ROUTINE rather than
 * by a hardcoded address. The DirectInput layer uses it to find the game's
 * gamepad re-enumeration routine: it is called from inside that routine, and
 * asking "which function am I in" is self-identifying in a way that a constant
 * in this repository would not be.
 *
 * It is an APPROXIMATION and says so: the export table carries entry points,
 * not sizes, and it names only what the image chose to export, so an address
 * inside an unexported function is attributed to the exported one before it.
 * Callers must sanity-check what comes back -- the one here checks the name --
 * rather than trusting the answer blind.
 */
uint32_t x86_native_entry_containing(uint32_t addr, const char **name_out) {
  X86Module *m = x86_module_for(addr);
  uint32_t rva;
  if (name_out)
    *name_out = NULL;
  if (!m)
    return 0;
  rva = pe_export_containing(*m->base, addr - *m->base, name_out);
  return rva ? *m->base + rva : 0; /* MAPPED address */
}

/* The name of the function that STARTS at a mapped address, or NULL. NULL is
   the common answer and an honest one: only exported entry points have a name
   here, and these images export a fraction of what they contain. */
const char *x86_native_name_at(uint32_t addr) {
  X86Module *m = x86_module_for(addr);
  const char *name = NULL;
  if (!m)
    return NULL;
  if (pe_export_containing(*m->base, addr - *m->base, &name) != addr - *m->base)
    return NULL;
  return name;
}

/* ---- native import thunks ---------------------------------------------
 *
 * Some imports are implemented natively but are not another guest module,
 * so binding cannot point their IAT slot at a guest body. Their IAT entries
 * receive synthetic guest addresses owned by the host-import dispatcher.
 *
 * So each such slot gets a synthetic address in a range this dispatcher owns,
 * and a call to one runs the stub. The range is deliberately NOT mapped: it is
 * never executed as code and never dereferenced, so leaving it unmapped means
 * a stray READ of one faults instead of returning a plausible word.
 *
 * The test for "implemented natively" is the one thing here that must not be
 * assumed: it compares the stub's address against the aborting default. A
 * module whose import is still the weak stub gets no thunk, and stays
 * poisoned, so nothing is silently promoted to "working".
 */
static struct {
  void (*stub)(CPU *);
  const char *mod, *sym;
  void *ctx;
} g_thunk[THUNK_MAX];
static int g_nthunk;

/*
 * RAW per-thunk call counts, immune to the boundary ring's repeat-collapse.
 *
 * The ring collapses consecutive identical crossings into one entry (with a
 * count), which is right for showing history and WRONG for measuring: a hot
 * import called in a loop reads as a handful of ring entries, so "how many
 * host calls does a build frame make and to which import" needs a counter
 * that increments on EVERY call. g_ring_n under-counts exactly the tight
 * loops a load hotspot is. This grows one 8-byte word per thunk, no smoothing;
 * the read side is the per-interval probe in x86_thunk_crossings_sorted.
 */
static unsigned long g_thunk_hits[THUNK_MAX];

void x86_thunk_record_hit(uint32_t idx) {
  if (idx < THUNK_MAX)
    g_thunk_hits[idx]++;
}

/* The context of the callback currently executing, for x86_callback_ctx. A
   native class's hooks are shared C functions -- what distinguishes one
   class's getClassMetaSafe from another's is the synthetic address the guest
   called, so the dispatcher hands that identity to the callee. */
static void *g_cb_ctx;

/*
 * A guest-callable address for an import this host implements.
 *
 * The answer comes from the host import registry (host_imports.h), which is a
 * property of this binary: an entry in the registry is an implementation.
 *
 * `sym` NULL means look up `ordinal` instead; WS2_32 is imported that way.
 *
 * The same import asked for twice gets the same address: GetProcAddress can be
 * called in a loop, and minting a fresh thunk per call both exhausts the table
 * and makes two pointers to one function compare unequal.
 */
uint32_t x86_native_thunk_at(const char *mod, const char *sym,
                             uint32_t ordinal) {
  const char *dll = NULL;
  const HostImport *e = host_import_find(mod, sym, ordinal, &dll);
  int i;
  if (!e)
    return 0;
  for (i = 0; i < g_nthunk; i++)
    if (g_thunk[i].stub == e->stub && g_thunk[i].mod == dll)
      return THUNK_BASE + (uint32_t)i * 16u;
  if (g_nthunk == THUNK_MAX)
    return 0;
  /* The registry's strings, not the caller's: `mod` and `sym` can point into
     a mapped image's import directory, and a report reads them much later. */
  g_thunk[g_nthunk].stub = e->stub;
  g_thunk[g_nthunk].mod = dll;
  g_thunk[g_nthunk].sym = e->sym;
  g_nthunk++;
  return THUNK_BASE + (uint32_t)(g_nthunk - 1) * 16u;
}

uint32_t x86_native_thunk(const char *mod, const char *sym) {
  return sym ? x86_native_thunk_at(mod, sym, 0) : 0;
}

/*
 * A synthetic guest address for a native C function that is NOT an import.
 *
 * The thunk range above exists so guest code can call native implementations of
 * things it imports. Substituting an engine class through ARK needs the same
 * trick for a different reason: libIGCore is handed function POINTERS at
 * registration (getClassMetaSafe, retrieveVTablePointer, arkRegisterInitialize)
 * and calls them back later, and every slot of the class's vtable is a pointer
 * the engine will dispatch through. Those must be addresses the guest can call,
 * and a host function pointer is 64 bits and in the wrong address space.
 *
 * Same table, same dispatch, same ring entries -- the only difference is that
 * the slot is claimed directly rather than found by import name. `owner` and
 * `name` are what a boundary-ring line or a fault report will say, so they are
 * required: an anonymous callback is one that cannot be identified in the very
 * report that needs to name it.
 */
uint32_t x86_native_callback(void (*fn)(CPU *), const char *owner,
                             const char *name, void *ctx) {
  if (!fn || !owner || !name) {
    x2_log_error("x86_native_callback: refusing to register an "
                 "unnamed or NULL callback (fn=%p owner=%s name=%s)\n",
                 (void *)fn, owner ? owner : "(null)", name ? name : "(null)");
    abort();
  }
  if (g_nthunk == THUNK_MAX) {
    x2_log_error("x86_native_callback: the %d-entry synthetic address "
                 "table is full; %s::%s cannot be given a guest "
                 "address. Raise THUNK_MAX.\n",
                 THUNK_MAX, owner, name);
    abort();
  }
  g_thunk[g_nthunk].stub = fn;
  g_thunk[g_nthunk].mod = owner;
  g_thunk[g_nthunk].sym = name;
  g_thunk[g_nthunk].ctx = ctx;
  g_nthunk++;
  return THUNK_BASE + (uint32_t)(g_nthunk - 1) * 16u;
}

void *x86_callback_ctx(void) { return g_cb_ctx; }

/*
 * Entry points of a module this host implements but NOTHING statically imports.
 *
 * x86_native_thunk above answers by searching the mapped modules' IMPORT
 * tables, which is right for a symbol some module links against -- and useless
 * for one the guest resolves at run time. XMen2.exe builds the path to
 * dinput8.dll from GetSystemDirectoryA, LoadLibraryAs it and asks for
 * DirectInput8Create by name; no import table mentions either, so there was
 * nothing for GetProcAddress to find and input was disabled wholesale
 * (issue #32).
 *
 * Registering here is also what makes "does this host implement that module"
 * answerable from ONE place: LoadLibraryA used to consult a hand-written list
 * of module names, which is a second source of truth that drifts from the set
 * of functions actually implemented.
 */
#define NATIVE_EXPORT_MAX 32
static struct {
  const char *mod, *sym;
  uint32_t addr;
} g_nexport[NATIVE_EXPORT_MAX];
static int g_nnexport;

void x86_native_export(const char *mod, const char *sym, void (*fn)(CPU *)) {
  if (g_nnexport == NATIVE_EXPORT_MAX) {
    x2_log_error("x86_native_export: the %d-entry table is full; "
                 "%s!%s cannot be published.\n",
                 NATIVE_EXPORT_MAX, mod, sym);
    abort();
  }
  g_nexport[g_nnexport].mod = mod;
  g_nexport[g_nnexport].sym = sym;
  g_nexport[g_nnexport].addr = x86_native_callback(fn, mod, sym, NULL);
  g_nnexport++;
}

uint32_t x86_native_export_addr(const char *mod, const char *sym) {
  int i;
  if (!mod || !sym)
    return 0;
  for (i = 0; i < g_nnexport; i++)
    if (strcasecmp(g_nexport[i].mod, mod) == 0 &&
        strcmp(g_nexport[i].sym, sym) == 0)
      return g_nexport[i].addr;
  return 0;
}

int x86_native_module_implemented(const char *mod) {
  int i;
  if (!mod)
    return 0;
  for (i = 0; i < g_nnexport; i++)
    if (strcasecmp(g_nexport[i].mod, mod) == 0)
      return 1;
  return 0;
}

void x86_native_export_report(void) {
  int i;
  if (!g_nnexport) {
    x2_log_info("  native exports: none registered -- no module is offered to "
                "LoadLibraryA beyond the ones this host maps.\n");
    return;
  }
  x2_log_info(
      "  native exports (resolvable by LoadLibraryA + GetProcAddress):\n");
  for (i = 0; i < g_nnexport; i++)
    x2_log_info("        %-14s %-24s 0x%08x\n", g_nexport[i].mod,
                g_nexport[i].sym, g_nexport[i].addr);
}

/* Which import a thunk address belongs to, for diagnostics. A thunk that is
   DEREFERENCED rather than called means the import is data, not a function --
   and a thunk cannot serve data, so that import needs a real value. */
const char *x86_thunk_name(uint32_t addr, const char **mod) {
  uint32_t i;
  if (addr < THUNK_BASE || addr >= THUNK_BASE + (uint32_t)THUNK_MAX * 16u)
    return NULL;
  i = (addr - THUNK_BASE) / 16u;
  if ((int)i >= g_nthunk)
    return NULL;
  *mod = g_thunk[i].mod;
  return g_thunk[i].sym;
}

static int thunk_call(uint32_t addr, CPU *C) {
  uint32_t i, in;
  if (addr < THUNK_BASE || addr >= THUNK_BASE + (uint32_t)THUNK_MAX * 16u)
    return 0;
  i = (addr - THUNK_BASE) / 16u;
  if ((int)i >= g_nthunk || !g_thunk[i].stub)
    return 0;
  in = C->reg[kX86pEsp];
  g_thunk_hits[i]++;
  g_sample_ep = addr;
  {
    void *save = g_cb_ctx;
    g_cb_ctx = g_thunk[i].ctx;
    if (x86_hotep_armed())
      span_push();
    g_thunk[i].stub(C);
    if (x86_hotep_armed())
      g_host_import_ns += span_pop();
    g_cb_ctx = save;
  }
  ring_note(g_thunk[i].sym, addr, 0, in, C->reg[kX86pEsp], 0);
  /* Imports are recorded TOO. A hand-written stub has to pop its own
     arguments the way the __stdcall function it replaces does, and getting
     that wrong shifts the guest stack by a word -- the exact failure the
     ring above was built to make visible, and the one path the stack check
     could not see, because this returns before the dispatch recorder. */
  if (g_sc_armed && g_sc_out) {
    g_sc_records++;
    fprintf(g_sc_out, "IMPORT:%s %08x %08x %08x\n",
            g_thunk[i].sym ? g_thunk[i].sym : "?", addr, in, C->reg[kX86pEsp]);
  }
  return 1;
}

/*
 * Is there something at this address that this dispatcher would RUN?
 *
 * The lookup half of x86_native_call_at, without the running half and without
 * any of its side effects -- no trigger arming, no entry-point counting, no
 * stack-check bookkeeping. The runtime execution engine needs to ask the
 * question at every instruction it executes: guest code can CALL an import
 * thunk or a native override, and both are HOST code that the JIT must not
 * decode as guest instructions.
 *
 * Kept beside x86_native_call_at deliberately, and in the same order, because
 * a disagreement between the two would be silent: the engine would either walk
 * into host memory (owned, not reported) or hand back a call the dispatcher
 * cannot make (reported, not owned). Change one, change the other.
 */
int x86_native_body_at(uint32_t addr) {
  if (x86_is_thunk(addr)) {
    uint32_t t = (addr - THUNK_BASE) / 16u;
    return (int)t < g_nthunk && g_thunk[t].stub ? 1 : 0;
  }
  return x86_override_bloom_has(addr) && owned_has(addr);
}

/* ---- the boundary ring -------------------------------------------------
 *
 * A snapshot at the failure says where execution ended up, not how it got
 * there, so the runtime owns one ring shared by its dispatcher and reports.
 *
 * It records ESP on both sides of every crossing, because the failure this was
 * built for is an ESP imbalance -- a hand-written import that pops the wrong
 * number of arguments shifts the guest stack by a word, and the damage appears
 * at some later RET that picks up the wrong word entirely. The imbalance is
 * invisible in a backtrace and obvious in a column of ESP values.
 */
#define RING 96
/* `base` distinguishes the two address spaces this ring records. Host-side
   crossings note a MAPPED address (base 0); the per-body trace hook receives
   its own LINKED entry point, so it also
   notes the module's runtime base. Without that the dump decoded a linked ep as
   a mapped one and confidently attributed libIGCore functions to libIGUtils --
   an instrument reporting the wrong module is worse than one reporting none. */
static struct {
  const char *what;
  uint32_t addr, base, esp_in, esp_out;
  /*
   * The caller's return address, for a body ENTRY -- 0 where there is none
   * to record.
   *
   * Without it the ring can show a two-body loop and say nothing about who
   * is running it, because the loop itself never crosses a boundary: issue
   * #35 sat on "something calls the frame timer forever" for a session
   * because the only thing the ring named was the timer. The guest-call
   * boundary already has this return word, so recording it costs one store.
   */
  uint32_t ret;
  unsigned repeat;
} g_ring[RING];
/* unsigned long, not unsigned: a run that reaches the main loop passes 2^32
   crossings in a trace build, and a counter that wraps would have the
   heartbeat report a negative delta as an enormous positive one. */
static unsigned long g_ring_n;

/*
 * Consecutive identical crossings collapse into one entry with a count.
 *
 * Without it a hot leaf drowns the ring: one four-instruction index helper
 * called in a loop filled all 96 slots with the same line, and the history
 * that mattered -- what happened BEFORE the imbalance -- had already scrolled
 * out. Capping the boring case rather than the interesting one is the whole
 * point of a ring this size.
 */
unsigned long x86_crossings(void) { return g_ring_n; }
unsigned int x86_thunk_count(void) { return (unsigned int)g_nthunk; }
unsigned int x86_thunk_capacity(void) { return (unsigned int)THUNK_MAX; }

/*
 * Per-interval import probe: the N most-called host imports between two reads.
 *
 * The heartbeat asks for this every period. The caller keeps a snapshot of the
 * cumulative counts and subtracts -- same torn-read trade as every counter the
 * heartbeat reads -- and gets back which imports the guest called the most in
 * the interval. THAT is the load-window question: the ring said the build
 * frames cross the boundary 400k+ times/frame and nothing else, and this names
 * the import behind it instead of guessing.
 *
 * Returns the number of imports written, sorted by delta, descending.
 */
unsigned int x86_thunk_crossings_sorted(unsigned long *snapshot,
                                        unsigned int snapshot_cap,
                                        const char **mod, const char **sym,
                                        unsigned long *hits, unsigned int cap) {
  unsigned int n = 0;
  int i;
  if (snapshot_cap < (unsigned int)g_nthunk) { /* see the header */
    x2_log_error("[HB] thunk probe: the snapshot holds %u entries, the "
                 "table now has %d\n",
                 snapshot_cap, g_nthunk);
    return 0;
  }
  for (i = 0; i < g_nthunk; i++) {
    unsigned long d = g_thunk_hits[i] - snapshot[i];
    int j;
    if (!d)
      continue;
    if (n == cap && d <= hits[cap - 1])
      continue; /* no room this round */
    if (n == cap)
      n--; /* drop the tail */
    for (j = (int)n - 1; j >= 0 && d > hits[j]; j--) {
      mod[j + 1] = mod[j];
      sym[j + 1] = sym[j];
      hits[j + 1] = hits[j];
    }
    mod[j + 1] = g_thunk[i].mod;
    sym[j + 1] = g_thunk[i].sym;
    hits[j + 1] = d;
    n++;
  }
  for (i = 0; i < g_nthunk; i++)
    snapshot[i] = g_thunk_hits[i];
  return n;
}

/* ---- the sampling profiler ----------------------------------------------
 *
 * WHY IT EXISTS. The hotep probe cannot name a load-window hotspot: the level
 * build dispatches ~460k DISTINCT entry points, a fixed hash table refuses
 * everything that collides, and the refused ones include the hot ones -- the
 * top-5 came back as 0.0ms in 1-2 dispatches while the interval's guest total
 * was 1400ms. A sampler does not need to see every EP: it reads the CURRENT
 * guest body every few ms and histograms the SAMPLES, so a body that runs a
 * lot is sampled a lot, whatever else ever ran.
 *
 * "Current guest body" is g_sample_ep, one store on every dispatch (both
 * guest bodies and import stubs), written by the thread running guest code
 * and read by the sampler thread. A 32-bit aligned store/load on x86 is
 * atomic, so a sample is never torn; it can only be stale by one dispatch,
 * which is exactly what a sample is supposed to be.
 *
 * Armed by X2_PROFILE=<period-ms>; reports at the end of the run through
 * x2_interrupt_reports (never only at a crash). The histogram prints its
 * sample total as the denominator, so "0 samples" is distinguishable from
 * "the probe never ran".
 */
#define PROFILE_MAX 1024
#define PROFILE_TOP 14
static struct {
  uint32_t ep;
  unsigned long n;
} g_profile[PROFILE_MAX];
static int g_profile_n;
static unsigned long g_profile_dropped, g_profile_total;

static void *profiler_thread(void *arg) {
  long period_ns = (long)(intptr_t)arg * 1000000L;
  struct timespec req = {0, (long)period_ns % 1000000000L};
  req.tv_sec = period_ns / 1000000000L;
  for (;;) {
    while (nanosleep(&req, &req) != 0 && errno == EINTR)
      ;
    if (x2_report_now)
      return NULL; /* let the heartbeat print the report */
    {
      uint32_t ep = g_sample_ep;
      int i;
      if (!ep)
        continue;
      g_profile_total++;
      for (i = 0; i < g_profile_n; i++)
        if (g_profile[i].ep == ep) {
          g_profile[i].n++;
          break;
        }
      if (i == g_profile_n) {
        if (g_profile_n < PROFILE_MAX) {
          g_profile[g_profile_n].ep = ep;
          g_profile[g_profile_n].n = 1;
          g_profile_n++;
        } else {
          g_profile_dropped++;
        }
      }
    }
  }
}

void x86_profiler_report(void) {
  int i, j;
  unsigned long shown = 0;
  x2_log_info("\n[PROF] %lu sample(s) of the running guest body",
              g_profile_total);
  if (g_profile_dropped)
    x2_log_info(" (%lu dropped past the %d-entry histogram)", g_profile_dropped,
                PROFILE_MAX);
  x2_log_info(", by entry point:\n");
  /* Top PROFILE_TOP by count, insertion-sorted like the hotep reader. */
  {
    uint32_t e[PROFILE_TOP];
    unsigned long c[PROFILE_TOP];
    int n = 0;
    for (i = 0; i < g_profile_n && n < PROFILE_TOP; i++) {
      for (j = n - 1; j >= 0 && g_profile[i].n > c[j]; j--) {
        e[j + 1] = e[j];
        c[j + 1] = c[j];
      }
      e[j + 1] = g_profile[i].ep;
      c[j + 1] = g_profile[i].n;
      if (n < PROFILE_TOP)
        n++;
    }
    for (i = 0; i < n; i++) {
      const char *nm = x86_native_name_at(e[i]);
      X86Module *m = x86_module_for(e[i]);
      shown += c[i];
      x2_log_info(
          "  %5.1f%% %lu  %s0x%08x (%s%s)\n",
          100.0 * (double)c[i] / (g_profile_total ? g_profile_total : 1), c[i],
          nm ? "" : "unresolved ", e[i], nm ? nm : (m ? m->name : "???"),
          (nm || !m) ? "" : " +offset");
    }
  }
  if (shown < g_profile_total)
    x2_log_info("  ... the other %lu sample(s) spread over %d more entry "
                "point(s)\n",
                g_profile_total - shown,
                g_profile_n > PROFILE_TOP ? g_profile_n - PROFILE_TOP : 0);
}

void x86_profiler_start(const char *arg) {
  long period_ms;
  pthread_t th;
  char *end = NULL;
  if (!arg || !*arg)
    return;
  period_ms = strtol(arg, &end, 10);
  if (period_ms <= 0 || (end && *end)) {
    x2_log_error("X2_PROFILE=%s is not a positive period in ms; the "
                 "sampler did not start.\n",
                 arg ? arg : "");
    return;
  }
  if (pthread_create(&th, NULL, profiler_thread, (void *)(intptr_t)period_ms) !=
      0) {
    x2_log_error("X2_PROFILE: could not start the sampler thread; "
                 "nothing will be sampled.\n");
    return;
  }
  pthread_detach(th);
  x2_log_error("X2_PROFILE=%ldms: sampling the running guest body every "
               "%ld ms; the histogram prints at the end of the run.\n",
               period_ms, period_ms);
}

/*
 * Wall-time split between host import stubs and guest bodies since the last
 * read -- the number the crossing COUNTS cannot give. Armed with X2_HOTEP;
 * unarmed, zeroes are returned and the heartbeat prints nothing.
 *
 * The split answers the load-window question in one line: "500k crossings per
 * frame" says the boundary is busy but not who paid for the 250ms. If the
 * host-import share is small, the cost is inside JIT-compiled guest blocks (a
 * translation/algorithm issue); if it is large, the cost is in the stubs
 * this host wrote (ReadFile, the heap, threads) and is ours to fix directly.
 */
/*
 * The exclusive-time span around ONE dispatched guest body. Ordinary guest
 * execution is owned by the JIT; x86_native_call_at runs only host imports and
 * native overrides, so a probe hooked there alone counts almost nothing while
 * reporting itself armed.
 *
 * A longjmp out of a nested call leaves the span depth high, which skews the
 * split for that interval; it does not lose the counters, and the probe is
 * opt-in diagnostics rather than a gate.
 */
void x86_probe_span_push(void) { span_push(); }

void x86_probe_guest_body_end(uint32_t ep) {
  unsigned long long excl = span_pop();
  g_guest_body_ns += excl;
  x86_hotep_count(ep, excl);
}

void x86_probe_time_delta(unsigned long long *host_import_ns,
                          unsigned long long *guest_body_ns) {
  static unsigned long long phost, pguest;
  /* One arming state, x86_hotep's: a second boolean here let count and
     timing disagree on Android, reporting "unarmed" after HOTEP started. */
  if (!x86_hotep_armed()) {
    *host_import_ns = *guest_body_ns = 0;
    return;
  }
  *host_import_ns = g_host_import_ns - phost;
  *guest_body_ns = g_guest_body_ns - pguest;
  phost = g_host_import_ns;
  pguest = g_guest_body_ns;
}

const char *x86_crossings_what(void) {
  return "host-boundary crossings only: a guest-to-guest call inside "
         "a compiled block crosses nothing and is invisible here";
}

static void ring_note(const char *what, uint32_t addr, uint32_t base,
                      uint32_t in, uint32_t out, uint32_t ret) {
  unsigned long i;
  if (g_ring_n) {
    i = (g_ring_n - 1) % RING;
    if (g_ring[i].addr == addr && g_ring[i].base == base &&
        g_ring[i].esp_in == in && g_ring[i].esp_out == out &&
        g_ring[i].ret == ret) {
      g_ring[i].repeat++;
      return;
    }
  }
  i = g_ring_n++ % RING;
  g_ring[i].what = what;
  g_ring[i].addr = addr;
  g_ring[i].base = base;
  g_ring[i].esp_in = in;
  g_ring[i].esp_out = out;
  g_ring[i].ret = ret;
  g_ring[i].repeat = 0;
}

/* These two live OUTSIDE the trace guard: the trace watch is not their
   only user any more -- X2_EPCOUNT decodes script names with them in an
   ordinary build, at report time -- and a second copy would be a second
   thing to get wrong. */
/*
 * Decoding a word as a STRING, and the two ways that goes wrong.
 *
 * Most of the arguments worth watching are char* -- a library name, a format
 * string, a class name -- and the watch printed them as hex, so answering
 * "which library failed to load" meant reading guest memory by hand after the
 * process was already gone. So the watch decodes them.
 *
 * A wild word dereferenced would take the process down inside the diagnostic,
 * which is the worst possible place for it, so every page is PROBED before it
 * is read. The probe is a write() of the range to /dev/null: the kernel does
 * the access check and answers EFAULT instead of raising SIGSEGV, so an
 * unmapped word costs an errno rather than the run.
 *
 * The first version bounded the read to mapped module images and live guest
 * heap blocks instead, and that was too narrow to answer the question it was
 * built for: the string it needed to read (a library name held by the game's
 * OWN CRT heap, which this host does not track block by block) fell outside
 * both and printed nothing. The probe has no such blind spot -- it asks the
 * kernel what is readable, which is the only authority on it.
 *
 * The second failure is the opposite one: printing 40 bytes of a struct as if
 * they were text. So it demands the whole prefix be printable, and stops at
 * the first byte that is not.
 */
/*
 * THE OLD PROBE VALIDATED NOTHING. It wrote the range to /dev/null and took a
 * full-length return as proof the memory was readable -- but Linux's null
 * device never copies from the buffer, so write() succeeds for a wild pointer
 * and the probe answered "readable" for every address. The decoder then
 * dereferenced it, and the report died inside the diagnostic with the faulting
 * address in hand (I048).
 *
 * process_vm_readv is what the rest of this file already uses to read guest
 * memory without risking a fault -- see x86_peek. It COPIES, so an unmapped
 * page comes back as an error instead of a signal.
 */
static int args_string_at(uint32_t a, char *out, size_t cap) {
  uint32_t i;

  if (a < 0x1000u)
    return 0;
  for (i = 0; i + 1 < cap; i++) {
    unsigned char c;
    if (!x86_peek(a + i, &c, 1))
      break; /* unmapped: stop, no fault */
    if (c == 0) {
      out[i] = 0;
      return i > 0;
    }
    if (c == '\n' || c == '\t') {
      out[i] = ' ';
      continue;
    }
    if (c < 0x20 || c > 0x7e)
      return 0;
    out[i] = (char)c;
  }
  /* Ran out of buffer, or off the end of what is mapped, with everything so
     far printable. Shown TRUNCATED rather than dropped: a long format string
     is exactly the kind of argument this watch exists to read, and dropping
     it printed nothing at all. The floor keeps three stray printable bytes
     from being announced as text. */
  if (i >= 8) {
    memcpy(out + i - 3, "...", 4);
    return 1;
  }
  return 0;
}

/* ---- guest-memory peek ------------------------------------------------
 *
 * "What was actually in that slot when it died?" -- the question every
 * cross-module data bug reduces to, and one the boundary ring cannot answer
 * because it records control flow, not state.
 *
 *   X2_PEEK=libIGCore+0x15f3fc:1,0x0067f708:4
 *
 * A bare 0x… is a GUEST/mapped address as-is; <module>+0x… is an offset from
 * that module's LINKED base, resolved through wherever it actually got mapped,
 * which is the form a Ghidra address can be pasted into.
 *
 * The read goes through the host's checked process-memory API rather than a
 * dereference: this runs
 * from a SIGSEGV handler, where a second fault would be a silent recursive
 * crash and the report would be lost -- so an unreadable address has to come
 * back as an error value, not as a signal.
 */
/* One safe read; 0 on failure. Never dereferences -- see x86_peek_report. */
static int process_read(uint32_t addr, void *dst, size_t n) {
  const void *source = guest_memory_const_pointer(addr);
#if defined(__APPLE__)
  mach_vm_size_t copied = 0;
  kern_return_t result = mach_vm_read_overwrite(
      mach_task_self(), (mach_vm_address_t)(uintptr_t)source, (mach_vm_size_t)n,
      (mach_vm_address_t)(uintptr_t)dst, &copied);
  return result == KERN_SUCCESS && copied == (mach_vm_size_t)n;
#else
  struct iovec loc, rem;
  loc.iov_base = dst;
  loc.iov_len = n;
  rem.iov_base = (void *)source;
  rem.iov_len = n;
#if defined(__ANDROID__)
  /* Bionic exposes the libc wrapper only from API 23, but the checked Linux
   * syscall exists at the API-21 64-bit floor. Keep the signal-handler-safe
   * read contract instead of replacing it with a faulting dereference. */
  return syscall(SYS_process_vm_readv, getpid(), &loc, 1, &rem, 1, 0) ==
         (ssize_t)n;
#else
  return process_vm_readv(getpid(), &loc, 1, &rem, 1, 0) == (ssize_t)n;
#endif
#endif
}

int x86_peek(uint32_t addr, void *dst, size_t n) {
  return process_read(addr, dst, n);
}

int x86_peek32(uint32_t addr, uint32_t *out) {
  return x86_peek(addr, out, sizeof *out);
}

static int peek_read(uint32_t addr, void *dst, size_t n) {
  return process_read(addr, dst, n);
}

/* Print up to `max` bytes at addr as a C string. Says why it printed nothing
   rather than printing an empty pair of quotes, which reads as "the string is
   empty" when it usually means the address was wrong. */
static void peek_string(uint32_t addr, unsigned max) {
  char s[129];
  unsigned i;
  if (max > sizeof s - 1)
    max = sizeof s - 1;
  for (i = 0; i < max; i++) {
    unsigned char c;
    if (!peek_read(addr + i, &c, 1)) {
      if (i == 0) {
        x2_log_error("UNREADABLE (not mapped)\n");
        return;
      }
      break;
    }
    if (!c)
      break;
    s[i] = (c >= 32 && c < 127) ? (char)c : '.';
  }
  s[i] = 0;
  if (!i)
    x2_log_error("\"\" (empty: first byte is NUL)\n");
  else
    x2_log_error("\"%s\"%s\n", s, i == max ? " (truncated)" : "");
}

/*
 * X2_PEEK=<place>[:<how>],...
 *
 *   <place>  0xABS                  a guest/mapped address as-is
 *            <module>+0xOFF         offset from that module's LINKED base,
 *                                   resolved through where it actually mapped,
 *                                   so a Ghidra address can be pasted in
 *   <how>    1 | 2 | 4              that many bytes, as a number (default 4)
 *            s | sN                 a C string AT that address, N bytes max
 *            *s | *sN               follow the dword there, then the string
 *            dN                     N consecutive dwords
 *
 * The *s and dN forms exist because the 4-byte-only version cost one whole run
 * per guess: identifying a meta field's name meant re-running the game once
 * for every candidate offset (issue #16).
 *
 * Every read goes through process_vm_readv rather than a dereference: this runs
 * from a SIGSEGV handler, where a second fault would be a silent recursive
 * crash and the report would be lost.
 */
void x86_peek_report(void) {
  const char *spec = x2_config_override_get(kX2ConfigPeek);
  /* 2 KB: a whole-object sweep is ~64 items and 512 bytes silently TRUNCATED
     the spec, so the tail of the sweep was simply not read. */
  char buf[2048], *p, *save;
  if (!spec || !*spec)
    return;
  snprintf(buf, sizeof buf, "%s", spec);
  { /* banner once: this now runs per watched call, and repeating the
       spec every time would bury the values it exists to show */
    static int banner;
    if (!banner) {
      x2_log_error("[PEEK] X2_PEEK=%s\n", spec);
      banner = 1;
    }
  }
  for (p = strtok_r(buf, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
    char item[128], *colon, *plus, how[24] = "4";
    unsigned size = 4, count = 1, i;
    uint32_t addr = 0;
    int resolved = 0, str = 0, deref = 0;
    unsigned char val[8];
    snprintf(item, sizeof item, "%s", p);
    if ((colon = strrchr(item, ':')) != NULL) {
      snprintf(how, sizeof how, "%s", colon + 1);
      *colon = 0;
    }
    {
      const char *h = how;
      if (*h == '*') {
        deref = 1;
        h++;
      }
      if (*h == 's') {
        str = 1;
        count = h[1] ? (unsigned)strtoul(h + 1, NULL, 0) : 48;
      } else if (*h == 'd') {
        count = h[1] ? (unsigned)strtoul(h + 1, NULL, 0) : 1;
        size = 4;
      } else if (deref) {
        str = 1;
        count = 48;
      } /* bare '*' means *s */
      else {
        size = (unsigned)strtoul(h, NULL, 0);
        if (size != 1 && size != 2 && size != 4) {
          x2_log_error("[PEEK]   %s: '%s' is not a size (1,2,4), a "
                       "string (s/sN/*s) or a dword run (dN) -- "
                       "NOT read\n",
                       item, how);
          continue;
        }
      }
    }
    if ((plus = strchr(item, '+')) != NULL) {
      X86Module *m;
      *plus = 0;
      for (m = x86_modules(); m; m = m->next)
        if (strcasecmp(m->name, item) == 0 ||
            (strncasecmp(m->name, item, strlen(item)) == 0 &&
             strchr(m->name, '.') == m->name + strlen(item))) {
          addr = *m->base + (uint32_t)strtoul(plus + 1, NULL, 0);
          resolved = 1;
          break;
        }
      if (!resolved) {
        x2_log_error("[PEEK]   %s+%s: NO module of that name is "
                     "registered -- nothing was read\n",
                     item, plus + 1);
        continue;
      }
      x2_log_error("[PEEK]   %s+%s -> mapped 0x%08x: ", item, plus + 1, addr);
    } else {
      addr = (uint32_t)strtoul(item, NULL, 0);
      x2_log_error("[PEEK]   0x%08x: ", addr);
    }
    if (deref) {
      if (!peek_read(addr, val, 4)) {
        x2_log_error("UNREADABLE (not mapped)\n");
        continue;
      }
      addr = (uint32_t)(val[0] | val[1] << 8 | val[2] << 16 |
                        (unsigned)val[3] << 24);
      x2_log_error("-> 0x%08x ", addr);
      if (!addr) {
        x2_log_error("(NULL, so no string to read)\n");
        continue;
      }
    }
    if (str) {
      peek_string(addr, count);
      continue;
    }
    for (i = 0; i < count; i++) {
      if (!peek_read(addr + i * size, val, size)) {
        x2_log_error("%sUNREADABLE (not mapped)", i ? " " : "");
        break;
      }
      if (size == 1)
        x2_log_error("%s0x%02x", i ? " " : "", val[0]);
      else if (size == 2)
        x2_log_error("%s0x%04x", i ? " " : "",
                     (unsigned)(val[0] | val[1] << 8));
      else
        x2_log_error("%s0x%08x", i ? " " : "",
                     (unsigned)(val[0] | val[1] << 8 | val[2] << 16 |
                                (unsigned)val[3] << 24));
    }
  }
}

/*
 * Everything the process knows, at any stop.
 *
 * The abort paths called only x86_ring_dump(), and abort() does not run atexit
 * handlers. Stop diagnostics are therefore invoked directly before abort;
 * an instrument that goes quiet when the run fails is not an instrument.
 */

void x86_ring_dump(void) {
  unsigned long n = g_ring_n < RING ? g_ring_n : RING, i;
  if (!g_ring_n) {
    x2_log_error("[TRACE] the boundary ring is EMPTY: nothing crossed "
                 "between guest and host before this point.\n");
    return;
  }
  x2_log_error("[TRACE] last %lu of %lu crossings (esp in -> out; a delta "
               "that is not 4+4N for a stdcall import is the bug):\n",
               n, g_ring_n);
  for (i = g_ring_n - n; i < g_ring_n; i++) {
    unsigned k = i % RING;
    uint32_t a = g_ring[k].addr;
    /* The ring records the MAPPED address, but every module here is linked
       for 0x10000000 and relocated elsewhere, so a mapped address matches
       nothing in Ghidra, in docs/, or in a seed file. Print the guest
       address it corresponds to, and its name when one is known -- without
       this the reader has to redo the relocation arithmetic by hand for
       every line, which is how a 96-line ring stayed unread. */
    uint32_t b = g_ring[k].base;
    X86Module *m = NULL;
    uint32_t guest = a;
    const char *nm = NULL;
    if (b) {
      /* `a` is a LINKED entry point in the module whose runtime base is
         `b`. Resolve by base, never by treating the ep as an address. */
      for (m = x86_modules(); m; m = m->next)
        if (*m->base == b)
          break;
      nm = m ? x86_native_name_at(b + (a - m->preferred)) : NULL;
    } else {
      m = x86_module_for(a);
      guest = m ? m->preferred + (a - *m->base) : a;
      nm = x86_native_name_at(a);
    }
    x2_log_error("[TRACE]   %-22s esp %08x -> %08x  (%+d)  ", g_ring[k].what,
                 g_ring[k].esp_in, g_ring[k].esp_out,
                 (int)(g_ring[k].esp_out - g_ring[k].esp_in));
    if (m)
      x2_log_error("%s!0x%08x %s", m->name, guest, nm ? nm : "(unnamed)");
    else if (b)
      x2_log_error("0x%08x (linked ep; no module has base 0x%08x)", a, b);
    else
      x2_log_error("0x%08x (no registered module)", a);
    if (g_ring[k].ret) {
      /* The caller, by return address. Its enclosing function is not
         resolved here: only entry points are named, and a return address
         is by definition in the middle of one. The raw address is what a
         disassembly listing is indexed by, so it is what gets printed. */
      uint32_t r = g_ring[k].ret;
      X86Module *rm = x86_module_for(r);
      x2_log_error("  <- 0x%08x", rm ? rm->preferred + (r - *rm->base) : r);
      if (rm)
        x2_log_error(" in %s", rm->name);
    }
    if (g_ring[k].repeat)
      x2_log_error("  x%u identical", g_ring[k].repeat + 1);
  }
}

/* ---- the abort paths ---------------------------------------------------
 *
 * Each of these names what is missing and stops. None of them may return a
 * plausible value: the native build has no original image mapped alongside it
 * and no Windows loader resolved anything, so there is nothing honest to fall
 * back TO. Quietly running something else would make the result dishonest.
 */
/*
 * An address may be a poisoned import slot rather than code. The host owns
 * that table, so it supplies this; the weak default keeps the runtime usable
 * on its own. Without it, a poisoned slot reached by an indirect `call [iat]`
 * is reported as "no registered module", which reads as a linking problem
 * rather than as the unimplemented import it is -- measured, on the exe's first
 * run.
 */
__attribute__((weak)) const char *x86_poison_name(uint32_t addr,
                                                  const char **mod) {
  (void)addr;
  (void)mod;
  return NULL;
}

void x86_missing_import(const char *mod, const char *sym) {
  x2_log_error("x86_missing_import: %s!%s is not implemented natively.\n"
               "  This is the native import surface -- the work that "
               "replaces Wine.\n",
               mod, sym);
  /*
   * WHO asked for it. The import's name says what is missing; it does not say
   * which subsystem wanted it, and that is what decides whether the answer is
   * an implementation or a different design. Every guest CALL pushes
   * its return address before the stub runs, so the word at ESP names the
   * caller -- and it is checked against the module list rather than trusted,
   * because a wrong stack makes the return address wrong too.
   *
   * Reading it took a run with a trace build and a manual grep through the
   * boundary ring, on a ring the OTHER guest threads were also writing to.
   */
  if (g_cpu_current) {
    uint32_t ra = RD32(g_cpu_current->reg[kX86pEsp]);
    const char *nm = x86_native_name_at(ra);
    X86Module *rm = x86_module_for(ra);
    if (nm)
      x2_log_error("  asked for by 0x%08x -- %s\n", ra, nm);
    else if (rm)
      x2_log_error("  asked for by 0x%08x, inside %s (guest 0x%08x) "
                   "but not at a body this host can name\n",
                   ra, rm->name, rm->preferred + (ra - *rm->base));
    else
      x2_log_error("  the word at the guest ESP is 0x%08x, which is in "
                   "no module -- so the caller cannot be named and the "
                   "STACK is suspect too\n",
                   ra);
  } else {
    x2_log_error("  no guest CPU is current, so the caller cannot be "
                 "named -- this was reached from host code, not from a "
                 "guest body\n");
  }
  x86_diag_dump();
  abort();
}

void x86_guest_addr_of(uint32_t addr, const char **mod, uint32_t *guest) {
  X86Module *m = x86_module_for(addr);
  if (!m) {
    *mod = NULL;
    *guest = addr;
    return;
  }
  *mod = m->name;
  *guest = m->preferred + (addr - *m->base);
}

void x87_fault(const char *what) {
  /*
   * A modelled-x87 fault used to print four words and abort, which says
   * WHICH invariant broke and nothing about where. The stack depth belongs to
   * the canonical CPU state -- an x87 operation without its matching push, or
   * a body entered at the wrong place -- so the ring,
   * which names the last bodies entered and who called them, is exactly the
   * evidence needed and it was being thrown away.
   */
  x2_log_error("x87_fault: %s\n"
               "  This is the MODELLED x87 stack, so it is a translation "
               "defect, not a guest bug: some body pushed or popped a "
               "different number of times than the original.\n",
               what);
  /*
   * The CALLER, by host return address.
   *
   * The ring names the last bodies ENTERED, which is the neighbourhood; it
   * cannot name the instruction, and "somewhere in a large float routine" is
   * not a place. This host return address can identify a native override or
   * runtime helper; the boundary ring supplies the corresponding guest
   * neighbourhood. The binary is PIE, so the load base has to come off first.
   */
  {
    unsigned long ra = (unsigned long)__builtin_return_address(0);
    Dl_info di;
    if (dladdr((void *)ra, &di) && di.dli_fbase)
      x2_log_error("  the body that did it:  addr2line -fCe "
                   "<this binary> 0x%lx\n",
                   ra - (unsigned long)di.dli_fbase);
    else
      x2_log_error("  (dladdr could not give the load base, so the "
                   "return address 0x%lx cannot be turned into a file "
                   "offset here)\n",
                   ra);
  }
  x86_diag_dump();
  abort();
}

long double x87_require_st0(const CPU *C, const char *what) {
  long double value = 0.0L;
  if (!x86p_x87_get(&C->x87, 0, &value))
    x87_fault(what);
  return value;
}

/*
 * Call a guest function FROM host code.
 *
 * A guest body is entered with its return address already on the guest stack
 * -- every guest CALL pushes one -- and its RET pops it. Host
 * code that dispatches without pushing one therefore leaks 4 bytes of guest
 * stack per call, upward, and the damage is silent until ESP walks off the
 * top: measured as a SIGSEGV 64 bytes above the stack top after 51 guest
 * constructor calls, which reads as stack corruption rather than a missing
 * push.
 *
 * So the convention lives here once instead of at each call site.
 */
void x86_guest_call_args(CPU *C, uint32_t target, uint32_t callee_pop_bytes) {
  uint32_t before = C->reg[kX86pEsp];
  uint32_t expected = before + callee_pop_bytes;
  C->reg[kX86pEsp] -= 4;
  *(volatile uint32_t *)x86_guest_pointer(C->reg[kX86pEsp]) =
      0xDEADBEEFu; /* popped by RET */
  x86_dispatch(C, target);
  /*
   * The balance check. This is the ONE place host code calls guest code, so
   * it is the one place the guest stack can be checked against a value the
   * host knows independently: after popping the return address, a called
   * function must leave ESP at the entry value plus exactly the declared
   * callee-cleaned argument bytes. Any other result is an ABI mismatch; there
   * is no safe value to repair it to because either the declaration or the
   * callee is wrong.
   */
  if (C->reg[kX86pEsp] != expected) {
    const char *nm = x86_native_name_at(target);
    unsigned long ra = (unsigned long)__builtin_return_address(0);
    Dl_info di;
    x2_log_error("x86_guest_call: 0x%08x (%s) violated its stack "
                 "contract: ESP %08x -> %08x, expected %08x after "
                 "popping %u argument byte(s).\n",
                 target, nm ? nm : "?", before, C->reg[kX86pEsp], expected,
                 callee_pop_bytes);
    if (dladdr((void *)ra, &di) && di.dli_fbase)
      x2_log_error("  host caller: addr2line -fCe <this binary> "
                   "0x%lx\n",
                   ra - (unsigned long)di.dli_fbase);
    else
      x2_log_error("  host caller could not be resolved (return "
                   "address 0x%lx).\n",
                   ra);
    x86_diag_dump();
    abort();
  }
}

void x86_guest_call(CPU *C, uint32_t target) {
  x86_guest_call_args(C, target, 0u);
}
