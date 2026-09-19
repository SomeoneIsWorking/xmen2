#ifndef X2_X86_ENGINE_JIT_POOL_H
#define X2_X86_ENGINE_JIT_POOL_H

#include "cpu.h"
#include "jit_engine.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct X86EngineJitPool X86EngineJitPool;

/* Guest execution and mapping changes are serialized by threads.c's guest lock.
 */
X86EngineJitPool *x86_engine_jit_pool_create(const X86pMem *mem, char *reason,
                                             unsigned reason_len);
X86pJitEngine *x86_engine_jit_pool_current(X86EngineJitPool *pool, char *reason,
                                           unsigned reason_len);
void x86_engine_jit_pool_publish_stats(X86EngineJitPool *pool,
                                       X86pJitEngine *jit);
void x86_engine_jit_pool_detach_current(X86EngineJitPool *pool);
int x86_engine_jit_pool_invalidate(X86EngineJitPool *pool, uint32_t address,
                                   uint32_t size, char *reason,
                                   unsigned reason_len);
void x86_engine_jit_pool_stats(const X86EngineJitPool *pool,
                               X86pJitEngineStats *out);
const X86pJitEngine *x86_engine_jit_pool_primary(const X86EngineJitPool *pool);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
