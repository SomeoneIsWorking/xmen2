/*
 * override_leaf.h -- native overrides a direct guest CALL completes in place.
 *
 * A guest CALL to a native override otherwise ends the JIT block, hands the
 * address back to the dispatcher, runs the override there and resumes at the
 * return address: two block exits and a dispatcher pass for what is often a
 * few loads and stores. x86port can instead call a leaf from inside the
 * translated CALL (x86p_jit_engine_set_leaves). A leaf entered with the return
 * address pushed either completes the whole call -- its result, its RET and
 * whatever arguments the callee pops -- and returns 1, or changes nothing and
 * returns 0, and the CALL then reaches the override the ordinary way.
 *
 * A leaf runs inside a translated block, so it must never run guest code, wait,
 * or give up the guest lock: any of those can retire the block the leaf returns
 * into. An override that can need its guest body has the body's cases decline.
 *
 * Each leaf is the native half of its own override, which runs the leaf first
 * and its guest body when the leaf declines, so the two paths share one
 * implementation of every answer.
 */
#ifndef X2_OVERRIDE_LEAF_H
#define X2_OVERRIDE_LEAF_H

#include "jit_engine.h"
#include "x86rt.h"

#include <stdint.h>

typedef int (*x86_override_leaf_fn)(CPU *C);

/* Declare `leaf` as the override at module!linked_ep's in-place form. The
   override itself is registered with x86_register_override as before. */
void x86_register_override_leaf(const char *module, uint32_t linked_ep,
                                x86_override_leaf_fn leaf);

/* x86port's leaf resolver: the leaf for a direct CALL to mapped `target`. */
X86pJitLeafFn x86_override_leaf_at(uint32_t target, void *user);

/* Install the resolver on `jit` unless `jit.leaves=0` or the stack check is
   configured, whose record every override call must reach. */
int x86_override_leaves_install(X86pJitEngine *jit, char *reason,
                                unsigned reason_len);

/* One line: per leaf, calls completed in place and calls declined. */
void x86_override_leaves_report(void);

#endif
