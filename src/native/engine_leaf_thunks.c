/*
 * Fast-path dispatch for pure leaf host import thunks.
 *
 * Leaf thunks (_ftol, _stricmp, QueryPerformanceCounter, toupper, etc.)
 * execute in a few host instructions and do not touch the full substrate state,
 * x87 depth translation, or register bridging. When cpu->eip lands on one of
 * these thunks, engine_leaf_thunk_dispatch services the call directly on the
 * x86port CPU state, bypassing x2_engine_callout_from_x86p, x86_dispatch, and
 * x2_engine_callout_to_x86p.
 */
#include "engine_leaf_thunks.h"

#include "guest_memory.h"
#include "winmm.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "x87.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <lucent/cvar_c.h>

uint32_t k32_tls_get_value(uint32_t index);

static X86pLeafHandler s_leaf_handlers[THUNK_MAX];
static int s_leaf_inited = 0;
static int s_leaf_enabled = 1;

void engine_leaf_thunks_enable(int enable) {
  s_leaf_enabled = enable ? 1 : 0;
}

int engine_leaf_thunks_is_enabled(void) {
  return s_leaf_enabled;
}

static int leaf_ftol(struct X86pCpu *cpu) {
  long double val = 0.0L;
  x86p_x87_pop(&cpu->x87, &val);
  const int64_t result = (int64_t)val;
  cpu->reg[kX86pEax] = (uint32_t)(uint64_t)result;
  cpu->reg[kX86pEdx] = (uint32_t)((uint64_t)result >> 32);
  const uint32_t ret = RD32(cpu->reg[kX86pEsp]);
  cpu->reg[kX86pEsp] += 4u;
  cpu->eip = ret;
  return 1;
}

static int leaf_stricmp(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const uint32_t s1_addr = RD32(esp + 4u);
  const uint32_t s2_addr = RD32(esp + 8u);
  const char *s1 = (const char *)guest_memory_const_pointer(s1_addr);
  const char *s2 = (const char *)guest_memory_const_pointer(s2_addr);
  if (__builtin_expect(!s1 || !s2, 0)) {
    cpu->reg[kX86pEax] = (s1 == s2) ? 0 : (s1 ? 1 : (uint32_t)-1);
  } else {
    cpu->reg[kX86pEax] = (uint32_t)strcasecmp(s1, s2);
  }
  cpu->reg[kX86pEsp] = esp + 4u;
  cpu->eip = ret;
  return 1;
}

static int leaf_qpc(struct X86pCpu *cpu) {
  winmm_timers_pump();
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t v = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const uint32_t out_addr = RD32(esp + 4u);
  WR32(out_addr, (uint32_t)v);
  WR32(out_addr + 4u, (uint32_t)(v >> 32));
  cpu->reg[kX86pEax] = 1u;
  cpu->reg[kX86pEsp] = esp + 8u;
  cpu->eip = ret;
  return 1;
}

static int leaf_qpf(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const uint32_t out_addr = RD32(esp + 4u);
  WR32(out_addr, 1000000000u);
  WR32(out_addr + 4u, 0u);
  cpu->reg[kX86pEax] = 1u;
  cpu->reg[kX86pEsp] = esp + 8u;
  cpu->eip = ret;
  return 1;
}

static int leaf_toupper(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const int c = (int)RD32(esp + 4u);
  cpu->reg[kX86pEax] = (uint32_t)toupper(c);
  cpu->reg[kX86pEsp] = esp + 4u;
  cpu->eip = ret;
  return 1;
}

static int leaf_tolower(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const int c = (int)RD32(esp + 4u);
  cpu->reg[kX86pEax] = (uint32_t)tolower(c);
  cpu->reg[kX86pEsp] = esp + 4u;
  cpu->eip = ret;
  return 1;
}

static int leaf_strstr(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const uint32_t h_addr = RD32(esp + 4u);
  const uint32_t n_addr = RD32(esp + 8u);
  const char *h = (const char *)guest_memory_const_pointer(h_addr);
  const char *n = (const char *)guest_memory_const_pointer(n_addr);
  if (__builtin_expect(h && n, 1)) {
    const char *res = strstr(h, n);
    cpu->reg[kX86pEax] = res ? guest_memory_address(res) : 0u;
  } else {
    cpu->reg[kX86pEax] = 0u;
  }
  cpu->reg[kX86pEsp] = esp + 4u;
  cpu->eip = ret;
  return 1;
}

static int leaf_tls_get_value(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const uint32_t index = RD32(esp + 4u);
  cpu->reg[kX86pEax] = k32_tls_get_value(index);
  cpu->reg[kX86pEsp] = esp + 8u;
  cpu->eip = ret;
  return 1;
}

int engine_leaf_thunk_register_at(uint32_t addr, X86pLeafHandler handler) {
  if (!x86_is_thunk(addr))
    return 0;
  const uint32_t idx = (addr - THUNK_BASE) >> 4;
  if (idx >= THUNK_MAX)
    return 0;
  s_leaf_handlers[idx] = handler;
  return 1;
}

int engine_leaf_thunk_register(const char *mod, const char *sym,
                               X86pLeafHandler handler) {
  const uint32_t addr = x86_native_thunk(mod, sym);
  if (!addr)
    return 0;
  return engine_leaf_thunk_register_at(addr, handler);
}

void engine_leaf_thunks_init(void) {
  if (s_leaf_inited)
    return;
  s_leaf_inited = 1;

  engine_leaf_thunk_register("MSVCR71.DLL", "_ftol", leaf_ftol);
  engine_leaf_thunk_register("MSVCRT.DLL", "_ftol", leaf_ftol);
  engine_leaf_thunk_register("MSVCR71.DLL", "_stricmp", leaf_stricmp);
  engine_leaf_thunk_register("MSVCRT.DLL", "_stricmp", leaf_stricmp);
  engine_leaf_thunk_register("MSVCR71.DLL", "_strcmpi", leaf_stricmp);
  engine_leaf_thunk_register("MSVCRT.DLL", "_strcmpi", leaf_stricmp);
  engine_leaf_thunk_register("KERNEL32.DLL", "QueryPerformanceCounter", leaf_qpc);
  engine_leaf_thunk_register("KERNEL32.DLL", "QueryPerformanceFrequency", leaf_qpf);
  engine_leaf_thunk_register("MSVCR71.DLL", "toupper", leaf_toupper);
  engine_leaf_thunk_register("MSVCRT.DLL", "toupper", leaf_toupper);
  engine_leaf_thunk_register("MSVCR71.DLL", "tolower", leaf_tolower);
  engine_leaf_thunk_register("MSVCRT.DLL", "tolower", leaf_tolower);
  engine_leaf_thunk_register("MSVCR71.DLL", "strstr", leaf_strstr);
  engine_leaf_thunk_register("MSVCRT.DLL", "strstr", leaf_strstr);
  engine_leaf_thunk_register("KERNEL32.DLL", "TlsGetValue", leaf_tls_get_value);

#if !defined(TEST_SUITE)
  s_leaf_enabled = lucent_cvar_flag("engine.leaf_thunks", 1) ? 1 : 0;
#endif
}

int engine_leaf_thunk_dispatch(struct X86pCpu *cpu) {
  if (__builtin_expect(!s_leaf_inited, 0))
    engine_leaf_thunks_init();

  if (!s_leaf_enabled)
    return 0;

  const uint32_t eip = cpu->eip;
  if (__builtin_expect(x86_is_thunk(eip), 0)) {
    const uint32_t idx = (eip - THUNK_BASE) >> 4;
    if (__builtin_expect(idx < THUNK_MAX && s_leaf_handlers[idx] != NULL, 1)) {
      x86_thunk_record_hit(idx);
      return s_leaf_handlers[idx](cpu);
    }
  }
  return 0;
}
