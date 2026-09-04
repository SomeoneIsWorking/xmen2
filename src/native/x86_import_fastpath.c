/*
 * Fast-path dispatch for eligible native imports.
 *
 * Imports such as _ftol, _stricmp, QueryPerformanceCounter, and toupper
 * execute directly against the canonical x86port CPU state. This is a narrow
 * title import optimization, not an alternate execution engine or CPU model.
 */
#include "x86_import_fastpath.h"

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

static X86ImportFastpathHandler s_import_handlers[THUNK_MAX];
static int s_imports_initialized = 0;
static int s_import_fastpath_enabled = 1;

void x86_import_fastpath_enable(int enable) {
  s_import_fastpath_enabled = enable ? 1 : 0;
}

int x86_import_fastpath_is_enabled(void) { return s_import_fastpath_enabled; }

static int import_ftol(struct X86pCpu *cpu) {
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

static int import_stricmp(struct X86pCpu *cpu) {
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

static int import_qpc(struct X86pCpu *cpu) {
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

static int import_qpf(struct X86pCpu *cpu) {
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

static int import_toupper(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const int c = (int)RD32(esp + 4u);
  cpu->reg[kX86pEax] = (uint32_t)toupper(c);
  cpu->reg[kX86pEsp] = esp + 4u;
  cpu->eip = ret;
  return 1;
}

static int import_tolower(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const int c = (int)RD32(esp + 4u);
  cpu->reg[kX86pEax] = (uint32_t)tolower(c);
  cpu->reg[kX86pEsp] = esp + 4u;
  cpu->eip = ret;
  return 1;
}

static int import_strstr(struct X86pCpu *cpu) {
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

static int import_tls_get_value(struct X86pCpu *cpu) {
  const uint32_t esp = cpu->reg[kX86pEsp];
  const uint32_t ret = RD32(esp);
  const uint32_t index = RD32(esp + 4u);
  cpu->reg[kX86pEax] = k32_tls_get_value(index);
  cpu->reg[kX86pEsp] = esp + 8u;
  cpu->eip = ret;
  return 1;
}

int x86_import_fastpath_register_at(uint32_t addr,
                                    X86ImportFastpathHandler handler) {
  if (!x86_is_thunk(addr))
    return 0;
  const uint32_t idx = (addr - THUNK_BASE) >> 4;
  if (idx >= THUNK_MAX)
    return 0;
  s_import_handlers[idx] = handler;
  return 1;
}

int x86_import_fastpath_register(const char *mod, const char *sym,
                                 X86ImportFastpathHandler handler) {
  const uint32_t addr = x86_native_thunk(mod, sym);
  if (!addr)
    return 0;
  return x86_import_fastpath_register_at(addr, handler);
}

void x86_import_fastpath_init(void) {
  if (s_imports_initialized)
    return;
  s_imports_initialized = 1;

  x86_import_fastpath_register("MSVCR71.DLL", "_ftol", import_ftol);
  x86_import_fastpath_register("MSVCRT.DLL", "_ftol", import_ftol);
  x86_import_fastpath_register("MSVCR71.DLL", "_stricmp", import_stricmp);
  x86_import_fastpath_register("MSVCRT.DLL", "_stricmp", import_stricmp);
  x86_import_fastpath_register("MSVCR71.DLL", "_strcmpi", import_stricmp);
  x86_import_fastpath_register("MSVCRT.DLL", "_strcmpi", import_stricmp);
  x86_import_fastpath_register("KERNEL32.DLL", "QueryPerformanceCounter",
                               import_qpc);
  x86_import_fastpath_register("KERNEL32.DLL", "QueryPerformanceFrequency",
                               import_qpf);
  x86_import_fastpath_register("MSVCR71.DLL", "toupper", import_toupper);
  x86_import_fastpath_register("MSVCRT.DLL", "toupper", import_toupper);
  x86_import_fastpath_register("MSVCR71.DLL", "tolower", import_tolower);
  x86_import_fastpath_register("MSVCRT.DLL", "tolower", import_tolower);
  x86_import_fastpath_register("MSVCR71.DLL", "strstr", import_strstr);
  x86_import_fastpath_register("MSVCRT.DLL", "strstr", import_strstr);
  x86_import_fastpath_register("KERNEL32.DLL", "TlsGetValue",
                               import_tls_get_value);

#if !defined(TEST_SUITE)
  s_import_fastpath_enabled =
      lucent_cvar_flag("engine.import_fastpath", 1) ? 1 : 0;
#endif
}

int x86_import_fastpath_dispatch(struct X86pCpu *cpu) {
  if (__builtin_expect(!s_imports_initialized, 0))
    x86_import_fastpath_init();

  if (!s_import_fastpath_enabled)
    return 0;

  const uint32_t eip = cpu->eip;
  if (__builtin_expect(x86_is_thunk(eip), 0)) {
    const uint32_t idx = (eip - THUNK_BASE) >> 4;
    if (__builtin_expect(idx < THUNK_MAX && s_import_handlers[idx] != NULL,
                         1)) {
      x86_thunk_record_hit(idx);
      return s_import_handlers[idx](cpu);
    }
  }
  return 0;
}
