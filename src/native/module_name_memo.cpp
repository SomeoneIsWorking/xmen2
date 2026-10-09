/* module_name_memo.cpp -- see module_name_memo.h. */
#include "module_name_memo.h"

#include <stdint.h>

namespace x2::native {

static unsigned first_slot(const char *name) {
  const uint64_t h = (uint64_t)(uintptr_t)name * 0x9E3779B97F4A7C15ull;
  return (unsigned)(h >> 56) % MODULE_NAME_MEMO_SLOTS;
}

struct X86Module *module_name_memo_get(ModuleNameMemo *memo, const char *name) {
  unsigned slot = first_slot(name);
  for (unsigned n = 0; n < MODULE_NAME_MEMO_SLOTS; n++) {
    const char *key = memo->key[slot].load(std::memory_order_acquire);
    if (key == name)
      return memo->module[slot].load(std::memory_order_acquire);
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
    if (memo->key[slot].compare_exchange_strong(
            key, name, std::memory_order_acq_rel, std::memory_order_acquire) ||
        key == name) {
      memo->module[slot].store(module, std::memory_order_release);
      return;
    }
    slot = (slot + 1u) % MODULE_NAME_MEMO_SLOTS;
  }
}

} // namespace x2::native
