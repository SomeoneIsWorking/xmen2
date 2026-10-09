#pragma once

/*
 * An insert-only map from a module-name POINTER to the module it named.
 *
 * Native overrides name the module whose guest body they run with a string
 * literal, once per call, and resolving that by strcmp down the module list
 * was a measurable share of every override that calls through. A registered
 * module is never removed, so once a pointer has named a module it names it
 * for the rest of the process: the answer is remembered per pointer, and a
 * second literal with the same text is only a second entry.
 *
 * Lock-free: overrides run on every guest thread. A slot's key is claimed
 * once and never changes; its module is published after it, so a reader
 * that finds the key before the module sees a miss and walks the list. A
 * full memo stores nothing more and every further name is a miss.
 */
#include <atomic>

struct X86Module;

namespace x2::native {

inline constexpr int MODULE_NAME_MEMO_SLOTS = 256;

struct ModuleNameMemo {
  std::atomic<const char *> key[MODULE_NAME_MEMO_SLOTS];
  std::atomic<struct X86Module *> module[MODULE_NAME_MEMO_SLOTS];
};

/* The module `name` was remembered to name, or NULL. */
struct X86Module *module_name_memo_get(ModuleNameMemo *memo, const char *name);

/* Remember that `name` names `module`. */
void module_name_memo_put(ModuleNameMemo *memo, const char *name,
                          struct X86Module *module);

} // namespace x2::native
