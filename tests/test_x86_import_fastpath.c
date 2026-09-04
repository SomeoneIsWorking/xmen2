/*
 * Unit tests for the native-import fast path
 * (src/native/x86_import_fastpath.c).
 */
#include "guest_memory.h"
#include "x86_import_fastpath.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "x87.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum {
  ARENA = 0x32000000u,
  ARENA_SIZE = 0x00100000u,
  STACK = ARENA + 0x20000u,
  STR1 = ARENA + 0x1000u,
  STR2 = ARENA + 0x1100u,
  RET_ADDR = 0x00405678u,
  OUT_BUF = ARENA + 0x3000u,
};

static unsigned failures = 0;
static uint32_t s_hit_count = 0;

void winmm_timers_pump(void) {}

void x86_thunk_record_hit(uint32_t idx) {
  (void)idx;
  s_hit_count++;
}

uint32_t k32_tls_get_value(uint32_t index) { return 0x77000000u | index; }

uint32_t x86_native_thunk(const char *mod, const char *sym) {
  if (!mod || !sym)
    return 0;
  if (!strcmp(sym, "_ftol"))
    return THUNK_BASE + 0x10u;
  if (!strcmp(sym, "_stricmp") || !strcmp(sym, "_strcmpi"))
    return THUNK_BASE + 0x20u;
  if (!strcmp(sym, "QueryPerformanceCounter"))
    return THUNK_BASE + 0x30u;
  if (!strcmp(sym, "QueryPerformanceFrequency"))
    return THUNK_BASE + 0x40u;
  if (!strcmp(sym, "toupper"))
    return THUNK_BASE + 0x50u;
  if (!strcmp(sym, "tolower"))
    return THUNK_BASE + 0x60u;
  if (!strcmp(sym, "strstr"))
    return THUNK_BASE + 0x70u;
  if (!strcmp(sym, "TlsGetValue"))
    return THUNK_BASE + 0x80u;
  return 0;
}

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static X86pCpu init_cpu(uint32_t esp) {
  X86pCpu cpu;
  memset(&cpu, 0, sizeof cpu);
  cpu.reg[kX86pEsp] = esp;
  cpu.reg[kX86pEbp] = esp + 0x100u;
  x86p_x87_reset(&cpu.x87);
  return cpu;
}

static void test_ftol_leaf(void) {
  X86pCpu cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  x86p_x87_push(&cpu.x87, 12345.0L);

  cpu.eip = x86_native_thunk("MSVCR71.DLL", "_ftol");
  int handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "ftol not handled by native-import fast path");
  CHECK(cpu.reg[kX86pEax] == 12345u, "ftol EAX incorrect");
  CHECK(cpu.reg[kX86pEdx] == 0u, "ftol EDX incorrect");
  CHECK(cpu.reg[kX86pEsp] == STACK + 4u, "ftol did not pop retaddr");
  CHECK(cpu.eip == RET_ADDR, "ftol did not set EIP to retaddr");
}

static void test_stricmp_leaf(void) {
  X86pCpu cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, STR1);
  WR32(STACK + 8u, STR2);

  strcpy((char *)guest_memory_pointer(STR1), "Hello World!");
  strcpy((char *)guest_memory_pointer(STR2), "HELLO WORLD!");

  uint32_t stricmp_addr = x86_native_thunk("MSVCR71.DLL", "_stricmp");
  cpu.eip = stricmp_addr;
  int handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "stricmp not handled by native-import fast path");
  CHECK(cpu.reg[kX86pEax] == 0u, "stricmp equal returned non-zero");
  CHECK(cpu.reg[kX86pEsp] == STACK + 4u, "stricmp did not pop retaddr");
  CHECK(cpu.eip == RET_ADDR, "stricmp did not set EIP");

  /* Test unequal strings */
  strcpy((char *)guest_memory_pointer(STR1), "Alpha");
  strcpy((char *)guest_memory_pointer(STR2), "Beta");
  cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, STR1);
  WR32(STACK + 8u, STR2);
  cpu.eip = stricmp_addr;
  handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "stricmp unequal not handled");
  CHECK((int32_t)cpu.reg[kX86pEax] < 0,
        "stricmp Alpha vs Beta should be negative");

  /* Test NULL pointer safety */
  cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, 0u);
  WR32(STACK + 8u, STR2);
  cpu.eip = stricmp_addr;
  handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "stricmp NULL not handled safely");
  CHECK(cpu.reg[kX86pEax] == (uint32_t)-1, "stricmp NULL vs non-NULL");
}

static void test_qpc_and_qpf_leaf(void) {
  X86pCpu cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, OUT_BUF);

  uint32_t qpf_addr =
      x86_native_thunk("KERNEL32.DLL", "QueryPerformanceFrequency");
  cpu.eip = qpf_addr;
  int handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "QPF not handled");
  CHECK(cpu.reg[kX86pEax] == 1u, "QPF return not 1");
  CHECK(cpu.reg[kX86pEsp] == STACK + 8u,
        "QPF did not pop 8 bytes (__stdcall 1 arg)");
  CHECK(RD32(OUT_BUF) == 1000000000u, "QPF frequency low not 10^9");
  CHECK(RD32(OUT_BUF + 4u) == 0u, "QPF frequency high not 0");

  cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, OUT_BUF);
  uint32_t qpc_addr =
      x86_native_thunk("KERNEL32.DLL", "QueryPerformanceCounter");
  cpu.eip = qpc_addr;
  handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "QPC not handled");
  CHECK(cpu.reg[kX86pEax] == 1u, "QPC return not 1");
  CHECK(cpu.reg[kX86pEsp] == STACK + 8u, "QPC did not pop 8 bytes");
  uint64_t v = (uint64_t)RD32(OUT_BUF) | ((uint64_t)RD32(OUT_BUF + 4u) << 32);
  CHECK(v > 0, "QPC time was zero");
}

static void test_toupper_tolower_strstr_tls(void) {
  X86pCpu cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, (uint32_t)'a');

  uint32_t toupper_addr = x86_native_thunk("MSVCR71.DLL", "toupper");
  cpu.eip = toupper_addr;
  int handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "toupper not handled");
  CHECK(cpu.reg[kX86pEax] == (uint32_t)'A', "toupper 'a' -> 'A'");
  CHECK(cpu.reg[kX86pEsp] == STACK + 4u, "toupper pop");

  cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, (uint32_t)'Z');
  uint32_t tolower_addr = x86_native_thunk("MSVCR71.DLL", "tolower");
  cpu.eip = tolower_addr;
  handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "tolower not handled");
  CHECK(cpu.reg[kX86pEax] == (uint32_t)'z', "tolower 'Z' -> 'z'");

  /* strstr */
  strcpy((char *)guest_memory_pointer(STR1), "The quick brown fox");
  strcpy((char *)guest_memory_pointer(STR2), "quick");
  cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, STR1);
  WR32(STACK + 8u, STR2);
  uint32_t strstr_addr = x86_native_thunk("MSVCR71.DLL", "strstr");
  cpu.eip = strstr_addr;
  handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "strstr not handled");
  CHECK(cpu.reg[kX86pEax] == STR1 + 4u, "strstr found substring offset");
  CHECK(cpu.reg[kX86pEsp] == STACK + 4u, "strstr pop");

  /* TlsGetValue */
  cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, 7u);
  uint32_t tls_addr = x86_native_thunk("KERNEL32.DLL", "TlsGetValue");
  cpu.eip = tls_addr;
  handled = x86_import_fastpath_dispatch(&cpu);
  CHECK(handled == 1, "TlsGetValue not handled");
  CHECK(cpu.reg[kX86pEax] == 0x77000007u, "TlsGetValue returned wrong value");
  CHECK(cpu.reg[kX86pEsp] == STACK + 8u, "TlsGetValue did not pop 8 bytes");
}

static void test_enable_disable(void) {
  X86pCpu cpu = init_cpu(STACK);
  WR32(STACK, RET_ADDR);
  WR32(STACK + 4u, (uint32_t)'x');
  uint32_t toupper_addr = x86_native_thunk("MSVCR71.DLL", "toupper");
  cpu.eip = toupper_addr;

  CHECK(x86_import_fastpath_is_enabled() == 1, "should default to enabled");
  CHECK(x86_import_fastpath_dispatch(&cpu) == 1, "should handle when enabled");

  x86_import_fastpath_enable(0);
  CHECK(x86_import_fastpath_is_enabled() == 0, "should report disabled");
  CHECK(x86_import_fastpath_dispatch(&cpu) == 0,
        "should return 0 when disabled");

  x86_import_fastpath_enable(1);
  cpu.eip = toupper_addr;
  CHECK(x86_import_fastpath_is_enabled() == 1, "should report re-enabled");
  CHECK(x86_import_fastpath_dispatch(&cpu) == 1,
        "should handle when re-enabled");
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA, ARENA_SIZE, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not map test arena\n");
    return 1;
  }

  x86_import_fastpath_init();

  test_ftol_leaf();
  test_stricmp_leaf();
  test_qpc_and_qpf_leaf();
  test_toupper_tolower_strstr_tls();
  test_enable_disable();

  CHECK(s_hit_count > 0, "hit recording did not increment");

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("x86_import_fastpath: ok");
  return 0;
}
