#ifndef X2_X86_IMPORT_FASTPATH_H
#define X2_X86_IMPORT_FASTPATH_H

#include <stdint.h>

struct X86pCpu;

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*X86ImportFastpathHandler)(struct X86pCpu *cpu);

/*
 * Fast-path dispatch for eligible native imports called from the JIT.
 *
 * Hot imports (_ftol, _stricmp, QueryPerformanceCounter, toupper, etc.)
 * execute directly against the canonical x86port CPU state. When cpu->eip
 * lands on one of these import thunks, this table avoids the general title
 * dispatch path while preserving the guest calling convention.
 *
 * Returns 1 if the thunk was handled, 0 if it should fall through to the
 * ordinary native-import dispatch.
 */
int x86_import_fastpath_dispatch(struct X86pCpu *cpu);

/* Explicit initialization of the import table. Safe to call multiple times.
 */
void x86_import_fastpath_init(void);

/* Enable or disable the native-import fast path (defaults to 1). */
void x86_import_fastpath_enable(int enable);
int x86_import_fastpath_is_enabled(void);

/* Register a custom handler for testing or overrides. */
int x86_import_fastpath_register(const char *mod, const char *sym,
                                 X86ImportFastpathHandler handler);
int x86_import_fastpath_register_at(uint32_t addr,
                                    X86ImportFastpathHandler handler);

#ifdef __cplusplus
}
#endif

#endif /* X2_X86_IMPORT_FASTPATH_H */
