/* override_leaf.c -- see override_leaf.h. */
#include "override_leaf.h"

#include "../config/environment.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X2_MAX_OVERRIDE_LEAVES 8

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

/* x86port calls a leaf with the CPU alone, so each table slot has its own
   entry, which counts that slot's answers. */
static inline int run_slot(unsigned slot, X86pCpu *cpu) {
  OverrideLeaf *leaf = &g_leaf[slot];
  if (leaf->fn(cpu)) {
    leaf->completed++;
    return 1;
  }
  leaf->declined++;
  return 0;
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
#undef X2_LEAF_SLOT

static const X86pJitLeafFn kLeafSlots[X2_MAX_OVERRIDE_LEAVES] = {
    leaf_slot_0, leaf_slot_1, leaf_slot_2, leaf_slot_3,
    leaf_slot_4, leaf_slot_5, leaf_slot_6, leaf_slot_7,
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

X86pJitLeafFn x86_override_leaf_at(uint32_t target, void *user) {
  (void)user;
  const char *module;
  uint32_t linked_ep;
  if (!x86_override_at(target, &module, &linked_ep)) {
    return NULL;
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
}
