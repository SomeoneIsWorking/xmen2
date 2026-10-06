/* module_name_memo.c -- see module_name_memo.h. */
#include "module_name_memo.h"

#include <stdint.h>

static unsigned first_slot(const char *name) {
  const uint64_t h = (uint64_t)(uintptr_t)name * 0x9E3779B97F4A7C15ull;
  return (unsigned)(h >> 56) % MODULE_NAME_MEMO_SLOTS;
}

struct X86Module *module_name_memo_get(ModuleNameMemo *memo, const char *name) {
  unsigned slot = first_slot(name);
  for (unsigned n = 0; n < MODULE_NAME_MEMO_SLOTS; n++) {
    const char *key =
        atomic_load_explicit(&memo->key[slot], memory_order_acquire);
    if (key == name)
      return atomic_load_explicit(&memo->module[slot], memory_order_acquire);
    if (!key)
      return NULL;
    slot = (slot + 1u) % MODULE_NAME_MEMO_SLOTS;
  }
  return NULL;
}

void module_name_memo_put(ModuleNameMemo *memo, const char *name,
                          struct X86Module *module) {
  unsigned slot = first_slot(name);
  for (unsigned n = 0; n < MODULE_NAME_MEMO_SLOTS; n++) {
    const char *key = NULL;
    if (atomic_compare_exchange_strong_explicit(&memo->key[slot], &key, name,
                                                memory_order_acq_rel,
                                                memory_order_acquire) ||
        key == name) {
      atomic_store_explicit(&memo->module[slot], module, memory_order_release);
      return;
    }
    slot = (slot + 1u) % MODULE_NAME_MEMO_SLOTS;
  }
}
