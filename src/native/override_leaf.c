/* override_leaf.c -- see override_leaf.h. */
#include "override_leaf.h"

#include "../config/environment.h"
#include "guest_memory.h"
#include "x2_log.h"
#include "x86_import_fastpath.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X2_MAX_OVERRIDE_LEAVES 16

/* Counted without atomics: a leaf runs in guest code, which one guest lock
   serializes across every guest thread. */
typedef struct OverrideLeaf {
  const char *module;
  uint32_t linked_ep;
  x86_override_leaf_fn fn;
  uint64_t completed;
  uint64_t declined;
} OverrideLeaf;

static OverrideLeaf g_leaf[X2_MAX_OVERRIDE_LEAVES];
static int g_nleaf;
/* JIT engines the resolver went into: 0 reads as "leaves off", not as "no
   leaf was called". */
static int g_engines;

/* Import thunks whose whole dispatch is leaf-safe, and the calls each leaf
   completed. Indexed by thunk slot. */
static uint8_t g_thunk_leaf[THUNK_MAX];
static int g_nthunk_leaf;
static uint64_t g_thunk_completed[THUNK_MAX];

/* The callee of the leaf this thread is running, 0 outside one: what the
   contract guard names. Per thread because the guard runs wherever a lock
   release or a guest call starts. */
static __thread uint32_t t_leaf_target;

/* x86port calls a leaf with the CPU alone, so each table slot has its own
   entry, which counts that slot's answers. */
static inline int run_slot(unsigned slot, X86pCpu *cpu) {
  OverrideLeaf *leaf = &g_leaf[slot];
  t_leaf_target = cpu->eip;
  const int done = leaf->fn(cpu);
  t_leaf_target = 0;
  if (done) {
    leaf->completed++;
    return 1;
  }
  leaf->declined++;
  return 0;
}

/* A thunk leaf runs the dispatcher's own path for the thunk -- the fast path
   first, as x86_engine_run_host_at does -- so the two cannot disagree. */
static inline int run_thunk(X86pCpu *cpu, int (*run)(X86pCpu *cpu)) {
  const uint32_t target = cpu->eip;
  t_leaf_target = target;
  const int done = run(cpu);
  t_leaf_target = 0;
  g_thunk_completed[(target - THUNK_BASE) >> 4] += (uint64_t)done;
  return done;
}

static int dispatch_thunk(X86pCpu *cpu) {
  return x86_native_thunk_call(cpu->eip, cpu);
}

static int fastpath_thunk_leaf(X86pCpu *cpu) {
  return run_thunk(cpu, x86_import_fastpath_dispatch);
}

static int thunk_leaf(X86pCpu *cpu) { return run_thunk(cpu, dispatch_thunk); }

/* The leaf for bound thunk `thunk`, or NULL when its dispatch is not
   leaf-safe. */
static X86pJitLeafFn thunk_leaf_for(uint32_t thunk) {
  if (x86_import_fastpath_leaf_safe(thunk)) {
    return fastpath_thunk_leaf;
  }
  return g_thunk_leaf[(thunk - THUNK_BASE) >> 4] ? thunk_leaf : NULL;
}

/*
 * The linker's import stub, `JMP [slot]`: every module in this title calls
 * its CRT imports (_ftol, _CIfmod) by a direct CALL to one, never by a CALL
 * on the slot. `words` are the stub's first two words; the answer is the
 * slot, or 0 when these are not a stub's bytes.
 */
#define X2_JMP_MEM_ABS 0x25ffu

static uint32_t import_stub_slot(uint32_t word0, uint32_t word1) {
  if ((word0 & 0xffffu) != X2_JMP_MEM_ABS) {
    return 0;
  }
  return (word0 >> 16) | (word1 << 16);
}

/* Entered at the stub, as its CALL enters it. The stub's bytes and its slot
   are read again on every call, so a patched stub or a rebound slot is
   followed, or declined, rather than trusted from translation time. */
static int import_stub_leaf(X86pCpu *cpu) {
  const uint32_t stub = cpu->eip;
  const uint32_t slot = import_stub_slot(RD32(stub), RD32(stub + 4u));
  const uint32_t thunk = slot ? RD32(slot) : 0u;
  const X86pJitLeafFn leaf = x86_is_thunk(thunk) ? thunk_leaf_for(thunk) : NULL;
  if (!leaf) {
    return 0;
  }
  cpu->eip = thunk;
  if (leaf(cpu)) {
    return 1;
  }
  cpu->eip = stub;
  return 0;
}

/* Asked while translating a direct CALL: whether `target` is an import stub
   whose slot holds a leaf-safe thunk now. */
static X86pJitLeafFn import_stub_leaf_at(uint32_t target) {
  uint32_t words[2];
  uint32_t thunk = 0;
  if (!guest_memory_try_read(target, words, sizeof words)) {
    return NULL;
  }
  const uint32_t slot = import_stub_slot(words[0], words[1]);
  if (!slot || !guest_memory_try_read32(slot, &thunk) || !x86_is_thunk(thunk) ||
      !thunk_leaf_for(thunk)) {
    return NULL;
  }
  return import_stub_leaf;
}

#define X2_LEAF_SLOT(n)                                                        \
  static int leaf_slot_##n(X86pCpu *cpu) { return run_slot(n##u, cpu); }
X2_LEAF_SLOT(0)
X2_LEAF_SLOT(1)
X2_LEAF_SLOT(2)
X2_LEAF_SLOT(3)
X2_LEAF_SLOT(4)
X2_LEAF_SLOT(5)
X2_LEAF_SLOT(6)
X2_LEAF_SLOT(7)
X2_LEAF_SLOT(8)
X2_LEAF_SLOT(9)
X2_LEAF_SLOT(10)
X2_LEAF_SLOT(11)
X2_LEAF_SLOT(12)
X2_LEAF_SLOT(13)
X2_LEAF_SLOT(14)
X2_LEAF_SLOT(15)
#undef X2_LEAF_SLOT

static const X86pJitLeafFn kLeafSlots[X2_MAX_OVERRIDE_LEAVES] = {
    leaf_slot_0,  leaf_slot_1,  leaf_slot_2,  leaf_slot_3,
    leaf_slot_4,  leaf_slot_5,  leaf_slot_6,  leaf_slot_7,
    leaf_slot_8,  leaf_slot_9,  leaf_slot_10, leaf_slot_11,
    leaf_slot_12, leaf_slot_13, leaf_slot_14, leaf_slot_15,
};

void x86_register_override_leaf(const char *module, uint32_t linked_ep,
                                x86_override_leaf_fn leaf) {
  for (int i = 0; i < g_nleaf; i++) {
    if (g_leaf[i].linked_ep == linked_ep && !strcmp(g_leaf[i].module, module)) {
      x2_log_error("x86_register_override_leaf: %s 0x%08x registered twice; "
                   "not continuing.\n",
                   module, linked_ep);
      abort();
    }
  }
  if (g_nleaf == X2_MAX_OVERRIDE_LEAVES) {
    x2_log_error("x86_register_override_leaf: the table holds %d and is "
                 "full; %s 0x%08x has no leaf. Raise X2_MAX_OVERRIDE_LEAVES "
                 "and add its slot.\n",
                 X2_MAX_OVERRIDE_LEAVES, module, linked_ep);
    abort();
  }
  g_leaf[g_nleaf].module = module;
  g_leaf[g_nleaf].linked_ep = linked_ep;
  g_leaf[g_nleaf].fn = leaf;
  g_nleaf++;
}

void x86_register_thunk_leaf(uint32_t thunk) {
  const char *module = NULL;
  const char *sym = x86_thunk_name(thunk, &module);
  if (!sym) {
    x2_log_error("x86_register_thunk_leaf: 0x%08x is no bound thunk; not "
                 "continuing.\n",
                 thunk);
    abort();
  }
  const uint32_t slot = (thunk - THUNK_BASE) >> 4;
  if (g_thunk_leaf[slot]) {
    x2_log_error("x86_register_thunk_leaf: %s!%s registered twice; not "
                 "continuing.\n",
                 module, sym);
    abort();
  }
  g_thunk_leaf[slot] = 1;
  g_nthunk_leaf++;
}

static void describe(uint32_t target, char *out, size_t len) {
  const char *module = NULL;
  const char *sym = x86_thunk_name(target, &module);
  uint32_t linked_ep = 0;
  if (sym) {
    snprintf(out, len, "%s!%s", module, sym);
  } else if (x86_override_at(target, &module, &linked_ep)) {
    snprintf(out, len, "%s!0x%08x", module, linked_ep);
  } else {
    snprintf(out, len, "0x%08x", target);
  }
}

void x86_override_leaf_forbid(const char *what) {
  const uint32_t target = t_leaf_target;
  if (!target) {
    return;
  }
  char name[128];
  describe(target, name, sizeof name);
  x2_log_error("\n*** the JIT leaf for %s %s. A leaf runs inside the "
               "translated block that called it, and anything that can run "
               "guest code or let another thread in can retire that block; "
               "not continuing.\n",
               name, what);
  abort();
}

X86pJitLeafFn x86_override_leaf_at(uint32_t target, void *user) {
  (void)user;
  if (x86_is_thunk(target)) {
    return thunk_leaf_for(target);
  }
  const char *module;
  uint32_t linked_ep;
  if (!x86_override_at(target, &module, &linked_ep)) {
    return import_stub_leaf_at(target);
  }
  for (int i = 0; i < g_nleaf; i++) {
    if (g_leaf[i].linked_ep == linked_ep && !strcmp(g_leaf[i].module, module)) {
      return kLeafSlots[i];
    }
  }
  return NULL;
}

int x86_override_leaves_install(X86pJitEngine *jit, char *reason,
                                unsigned reason_len) {
  const char *stack_check = x2_config_override_get(kX2ConfigStackCheck);
  if (!lucent_cvar_flag("jit.leaves", 1) || (stack_check && *stack_check)) {
    return 1;
  }
  if (!x86p_jit_engine_set_leaves(jit, x86_override_leaf_at, NULL, reason,
                                  reason_len)) {
    return 0;
  }
  g_engines++;
  return 1;
}

/* The busiest thunk leaves, most calls first; every other one is summed. */
#define X2_THUNK_LEAVES_NAMED 8

static int busier_first(const void *a, const void *b) {
  const uint64_t na = g_thunk_completed[*(const uint32_t *)a];
  const uint64_t nb = g_thunk_completed[*(const uint32_t *)b];
  return (na < nb) - (na > nb);
}

static void report_thunk_leaves(void) {
  uint32_t busy[THUNK_MAX];
  int nbusy = 0;
  uint64_t total = 0;
  for (uint32_t i = 0; i < THUNK_MAX; i++) {
    if (g_thunk_completed[i]) {
      total += g_thunk_completed[i];
      busy[nbusy++] = i;
    }
  }
  qsort(busy, (size_t)nbusy, sizeof busy[0], busier_first);
  const int ntop =
      nbusy < X2_THUNK_LEAVES_NAMED ? nbusy : X2_THUNK_LEAVES_NAMED;
  char line[640];
  size_t used = 0;
  line[0] = '\0';
  for (int k = 0; k < ntop && used < sizeof line; k++) {
    char name[128];
    describe(THUNK_BASE + busy[k] * 16u, name, sizeof name);
    const int n =
        snprintf(line + used, sizeof line - used, "%s %s %llu", k ? ";" : "",
                 name, (unsigned long long)g_thunk_completed[busy[k]]);
    if (n < 0) {
      break;
    }
    used += (size_t)n;
  }
  x2_log_info(
      "THUNK LEAVES: %d registered plus the leaf-safe fast-path "
      "imports; %llu call(s) completed in place across %d thunk(s)%s%s\n",
      g_nthunk_leaf, (unsigned long long)total, nbusy, ntop ? ", busiest:" : "",
      line);
}

void x86_override_leaves_report(void) {
  char line[512];
  size_t used = 0;
  line[0] = '\0';
  for (int i = 0; i < g_nleaf && used < sizeof line; i++) {
    const int n = snprintf(
        line + used, sizeof line - used, "%s %s!0x%08x %llu of %llu",
        i ? ";" : "", g_leaf[i].module, g_leaf[i].linked_ep,
        (unsigned long long)g_leaf[i].completed,
        (unsigned long long)(g_leaf[i].completed + g_leaf[i].declined));
    if (n < 0) {
      break;
    }
    used += (size_t)n;
  }
  x2_log_info("OVERRIDE LEAVES: %d registered, installed on %d JIT "
              "engine(s); calls completed in place of calls offered:%s\n",
              g_nleaf, g_engines, g_nleaf ? line : " none");
  report_thunk_leaves();
}
