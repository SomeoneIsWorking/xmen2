#ifndef ENGINE_LEAF_THUNKS_H
#define ENGINE_LEAF_THUNKS_H

#include <stdint.h>

struct X86pCpu;

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*X86pLeafHandler)(struct X86pCpu *cpu);

/*
 * Fast-path dispatch for pure leaf host import thunks called from the JIT.
 *
 * Hot leaf thunks (_ftol, _stricmp, QueryPerformanceCounter, toupper, etc.)
 * execute in a few host instructions and do not touch the full substrate state,
 * x87 depth translation, or register bridging. When cpu->eip lands on one of
 * these thunks, engine_leaf_thunk_dispatch services the call directly on the
 * x86port CPU state, bypassing x2_engine_callout_from_x86p, x86_dispatch, and
 * x2_engine_callout_to_x86p.
 *
 * Returns 1 if the thunk was handled, 0 if it should fall through to the
 * standard substrate dispatch bridge.
 */
int engine_leaf_thunk_dispatch(struct X86pCpu *cpu);

/* Explicit initialization of the leaf thunk table. Safe to call multiple times. */
void engine_leaf_thunks_init(void);

/* Enable or disable leaf thunk fast path (defaults to 1). */
void engine_leaf_thunks_enable(int enable);
int engine_leaf_thunks_is_enabled(void);

/* Register a custom leaf handler for testing or overrides. */
int engine_leaf_thunk_register(const char *mod, const char *sym,
                               X86pLeafHandler handler);
int engine_leaf_thunk_register_at(uint32_t addr, X86pLeafHandler handler);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_LEAF_THUNKS_H */
