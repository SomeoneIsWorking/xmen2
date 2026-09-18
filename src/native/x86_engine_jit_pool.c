#include "x86_engine_jit_pool.h"

#include "x86_engine_dispatch.h"
#include "x86_engine_intercept.h"
#include "x86_engine_jit_diag.h"

#include <lucent/cvar_c.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct X86EngineJitNode {
  X86pJitEngine *jit;
#if defined(__EMSCRIPTEN__)
  pthread_t owner;
  X86pJitEngineStats published_stats;
  int pending;
  int pending_all;
  uint32_t pending_lo;
  uint32_t pending_hi;
#endif
  struct X86EngineJitNode *next;
} X86EngineJitNode;

struct X86EngineJitPool {
  const X86pMem *mem;
  X86EngineJitNode *primary;
  X86EngineJitNode *live;
  X86pJitEngineStats retired;
#if defined(__EMSCRIPTEN__)
  pthread_mutex_t mutex;
#endif
};

#if defined(__EMSCRIPTEN__)
/*
 * Each browser worker owns its own JS module registry and indirect table, so
 * both numbers bound ONE guest thread.
 *
 * kCacheBlocks now sizes the live translated code as well as the cache that
 * names it: x86port used to hold a fixed 1,024 WebAssembly modules whatever
 * cache it was given, so 7 of every 8 entries here could never hold anything
 * and every translation past the thousandth evicted a block the game was
 * still running. MEASURED on the Dead Zone route before the fix: 7,244
 * retranslations per second, with 42.7% of the busy worker's samples inside
 * `new WebAssembly.Module`/`Instance` plus the host glue and another 9.8% in
 * the invalidation those evictions drive.
 *
 * kCodeBytes is a BUDGET, not an allocation: it bounds the bytes of live
 * module held at once and must stay clear of the block cap times the mean
 * block, or bytes silently become the binding limit again. The measured mean
 * is ~1.4 KB, so 8,192 blocks need ~12 MB and 32 MB leaves the cap where it
 * is meant to be.
 *
 * That cap is still below the working set. MEASURED on the Dead Zone route
 * with the heartbeat's eviction counters: 15,770 blocks translated and 15,770
 * dropped in the same five seconds, zero cache flushes, and 13.6 MB of the
 * 32 MB budget in use -- so the block cap binds and the arena turns over
 * completely several times a second (issue #161). Both sizes are settings
 * rather than constants so the working set can be found by running past it,
 * which is what has to happen before either default moves: a doubled cap that
 * makes this map stop evicting is a number chosen to hide the measurement.
 */
enum { kCodeBytesDefault = 32u << 20, kCacheBlocksDefault = 8192u };
static _Thread_local X86EngineJitNode *current_node;
#else
enum { kCodeBytesDefault = 64u << 20, kCacheBlocksDefault = 65536u };
#endif

/*
 * The code arena's two limits, as settings.
 *
 * Both are read once per engine, at creation, and a value that is not a
 * positive number is the default rather than a silent zero -- an arena of zero
 * blocks refuses every translation, which would present as the JIT being
 * absent rather than as a setting being wrong. The pair is deliberately not
 * one knob: blocks and bytes bind independently and knowing WHICH one stopped
 * a run is the whole point of being able to move them.
 */
static size_t jit_cache_blocks(void) {
  const long blocks = lucent_cvar_number("jit.blocks", 0);
  return blocks > 0 ? (size_t)blocks : (size_t)kCacheBlocksDefault;
}

static size_t jit_code_bytes(void) {
  const long megabytes = lucent_cvar_number("jit.code_mb", 0);
  return megabytes > 0 ? (size_t)megabytes << 20 : (size_t)kCodeBytesDefault;
}

static void pool_lock(X86EngineJitPool *pool) {
#if defined(__EMSCRIPTEN__)
  pthread_mutex_lock(&pool->mutex);
#else
  (void)pool;
#endif
}

static void pool_unlock(X86EngineJitPool *pool) {
#if defined(__EMSCRIPTEN__)
  pthread_mutex_unlock(&pool->mutex);
#else
  (void)pool;
#endif
}

static X86EngineJitNode *create_node(const X86pMem *mem, char *reason,
                                     unsigned reason_len) {
  X86EngineJitNode *node = calloc(1u, sizeof *node);
  if (!node) {
    snprintf(reason, reason_len, "out of memory creating a JIT thread record");
    return NULL;
  }
  node->jit = x86p_jit_engine_create(mem, jit_code_bytes(), jit_cache_blocks(),
                                     reason, reason_len);
  if (!node->jit) {
    free(node);
    return NULL;
  }
  x86p_jit_engine_set_intercept(node->jit, x86_engine_jit_intercept, NULL);
  if (lucent_cvar_flag("jit.inline_dispatch", 1)) {
    x86p_jit_engine_set_dispatch(node->jit, x86_engine_jit_dispatch, NULL);
  }
  x86p_jit_engine_set_boundary(node->jit, x86_engine_jit_boundary, NULL);
  if (!x86_engine_jit_diag_configure(node->jit, reason, reason_len)) {
    x86p_jit_engine_destroy(node->jit);
    free(node);
    return NULL;
  }
#if defined(__EMSCRIPTEN__)
  node->owner = pthread_self();
  x86p_jit_engine_stats(node->jit, &node->published_stats);
  current_node = node;
#endif
  return node;
}

X86EngineJitPool *x86_engine_jit_pool_create(const X86pMem *mem, char *reason,
                                             unsigned reason_len) {
  X86EngineJitPool *pool = calloc(1u, sizeof *pool);
  if (!pool) {
    snprintf(reason, reason_len, "out of memory creating the JIT pool");
    return NULL;
  }
  pool->mem = mem;
#if defined(__EMSCRIPTEN__)
  if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
    snprintf(reason, reason_len, "could not initialize the JIT pool lock");
    free(pool);
    return NULL;
  }
#endif
  pool->primary = create_node(mem, reason, reason_len);
  if (!pool->primary) {
#if defined(__EMSCRIPTEN__)
    pthread_mutex_destroy(&pool->mutex);
#endif
    free(pool);
    return NULL;
  }
  pool->live = pool->primary;
  return pool;
}

X86pJitEngine *x86_engine_jit_pool_current(X86EngineJitPool *pool, char *reason,
                                           unsigned reason_len) {
#if defined(__EMSCRIPTEN__)
  X86EngineJitNode *node;
  if (current_node) {
    int pending;
    int pending_all;
    uint32_t pending_lo;
    uint32_t pending_hi;
    pool_lock(pool);
    pending = current_node->pending;
    pending_all = current_node->pending_all;
    pending_lo = current_node->pending_lo;
    pending_hi = current_node->pending_hi;
    current_node->pending = 0;
    current_node->pending_all = 0;
    pool_unlock(pool);
    if (pending) {
      if (pending_all) {
        if (!x86p_jit_engine_invalidate_all(current_node->jit, reason,
                                            reason_len)) {
          return NULL;
        }
      } else {
        x86p_jit_engine_invalidate(current_node->jit, pending_lo, pending_hi);
      }
    }
    return current_node->jit;
  }
  node = create_node(pool->mem, reason, reason_len);
  if (!node) {
    return NULL;
  }
  pool_lock(pool);
  node->next = pool->live;
  pool->live = node;
  pool_unlock(pool);
  return node->jit;
#else
  (void)reason;
  (void)reason_len;
  return pool->primary->jit;
#endif
}

void x86_engine_jit_pool_publish_stats(X86EngineJitPool *pool,
                                       X86pJitEngine *jit) {
#if defined(__EMSCRIPTEN__)
  X86pJitEngineStats stats;
  if (!pool || !current_node || current_node->jit != jit) {
    abort();
  }
  x86p_jit_engine_stats(jit, &stats);
  pool_lock(pool);
  current_node->published_stats = stats;
  pool_unlock(pool);
#else
  (void)pool;
  (void)jit;
#endif
}

void x86_engine_jit_pool_detach_current(X86EngineJitPool *pool) {
#if defined(__EMSCRIPTEN__)
  X86EngineJitNode **cursor;
  X86pJitEngineStats stats;
  if (!pool || !current_node || current_node == pool->primary) {
    return;
  }
  x86p_jit_engine_stats(current_node->jit, &stats);
  pool_lock(pool);
  for (cursor = &pool->live; *cursor != NULL; cursor = &(*cursor)->next) {
    if (*cursor == current_node) {
      X86EngineJitNode *node = *cursor;
      *cursor = node->next;
      stats.code_bytes_used = 0u;
      x86p_jit_engine_stats_add(&pool->retired, &stats);
      pool_unlock(pool);
      x86p_jit_engine_destroy(node->jit);
      free(node);
      current_node = NULL;
      return;
    }
  }
  pool_unlock(pool);
  abort(); /* A thread-owned JIT escaped the pool's live list. */
#else
  (void)pool;
#endif
}

int x86_engine_jit_pool_invalidate(X86EngineJitPool *pool, uint32_t address,
                                   uint32_t size, char *reason,
                                   unsigned reason_len) {
  X86EngineJitNode *node;
  uint64_t end = (uint64_t)address + size;
  pool_lock(pool);
  for (node = pool->live; node != NULL; node = node->next) {
#if defined(__EMSCRIPTEN__)
    if (!pthread_equal(node->owner, pthread_self())) {
      if (end > UINT32_MAX) {
        node->pending_all = 1;
      } else if (!node->pending) {
        node->pending_lo = address;
        node->pending_hi = (uint32_t)end;
      } else {
        if (address < node->pending_lo) {
          node->pending_lo = address;
        }
        if (end > node->pending_hi) {
          node->pending_hi = (uint32_t)end;
        }
      }
      node->pending = 1;
      continue;
    }
#endif
    if (end > UINT32_MAX) {
      if (!x86p_jit_engine_invalidate_all(node->jit, reason, reason_len)) {
        pool_unlock(pool);
        return 0;
      }
    } else {
      x86p_jit_engine_invalidate(node->jit, address, (uint32_t)end);
    }
#if defined(__EMSCRIPTEN__)
    x86p_jit_engine_stats(node->jit, &node->published_stats);
#endif
  }
  pool_unlock(pool);
  return 1;
}

void x86_engine_jit_pool_stats(const X86EngineJitPool *pool,
                               X86pJitEngineStats *out) {
  const X86EngineJitNode *node;
  pool_lock((X86EngineJitPool *)pool);
  *out = pool->retired;
  for (node = pool->live; node != NULL; node = node->next) {
    X86pJitEngineStats current;
#if defined(__EMSCRIPTEN__)
    current = node->published_stats;
#else
    x86p_jit_engine_stats(node->jit, &current);
#endif
    x86p_jit_engine_stats_add(out, &current);
  }
  pool_unlock((X86EngineJitPool *)pool);
}

const X86pJitEngine *x86_engine_jit_pool_primary(const X86EngineJitPool *pool) {
  return pool->primary->jit;
}
