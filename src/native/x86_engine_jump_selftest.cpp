/* Two live guest jump buffers must retain distinct continuations even though
 * their host setjmp calls reuse the same runtime-loop stack frame. */
#include "guest_heap.h"
#include "guest_memory.h"
#include "x2_log.h"
#include "x86_engine.h"
#include "x86_engine_private.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <stdint.h>
#include <string.h>

static void append32(uint8_t *code, unsigned *length, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    code[(*length)++] = (uint8_t)(value >> (8 * i));
}
static unsigned checkpoint(uint8_t *code, unsigned *n, uint32_t env,
                           uint32_t target) {
  code[(*n)++] = 0x6a;
  code[(*n)++] = 0; /* push 0 */
  code[(*n)++] = 0x68;
  append32(code, n, env);
  code[(*n)++] = 0xb8;
  append32(code, n, target);
  code[(*n)++] = 0xff;
  code[(*n)++] = 0xd0;
  code[(*n)++] = 0x83;
  code[(*n)++] = 0xc4;
  code[(*n)++] = 8;
  code[(*n)++] = 0x85;
  code[(*n)++] = 0xc0;
  code[(*n)++] = 0x75; /* jnz continuation */
  unsigned branch = (*n)++;
  return branch;
}
int x2_engine_jump_selftest(uint32_t page, uint32_t stack) {
  const uint32_t save = x86_native_thunk("MSVCR71.DLL", "_setjmp3");
  const uint32_t jump = x86_native_thunk("MSVCR71.DLL", "longjmp");
  const uint32_t env = guest_malloc(128);
  uint8_t code[128] = {0};
  unsigned n = 0;
  CPU cpu;
  if (!save || !jump || !env) {
    x2_log_error("engine jump selftest: missing import or guest storage\n");
    if (env)
      guest_free(env);
    return 0;
  }
  unsigned first = checkpoint(code, &n, env, save);
  unsigned second = checkpoint(code, &n, env + 64, save);
  code[n++] = 0x6a;
  code[n++] = 7;
  code[n++] = 0x68;
  append32(code, &n, env);
  code[n++] = 0xb8;
  append32(code, &n, jump);
  code[n++] = 0xff;
  code[n++] = 0xd0;
  code[n++] = 0x0f;
  code[n++] = 0x0b; /* longjmp cannot return */
  code[first] = (uint8_t)(n - first - 1);
  code[n++] = 0xb8;
  append32(code, &n, 0x12345678);
  code[n++] = 0xc3;
  code[second] = (uint8_t)(n - second - 1);
  code[n++] = 0xb8;
  append32(code, &n, 0xBAD);
  code[n++] = 0xc3;
  memcpy(guest_memory_pointer(page), code, n);
  cpu_reset(&cpu);
  cpu.reg[kX86pEsp] = stack - 4;
  WR32(stack - 4, ENGINE_RETURN_ADDR);
  int ok = x2_engine_call(page, &cpu) && cpu.reg[kX86pEax] == 0x12345678 &&
           cpu.reg[kX86pEsp] == stack;
  guest_free(env);
  x86_setjmp_reclaim();
  x2_log_info("engine jump selftest: two saved continuations, older buffer "
              "resumed: %s\n",
              ok ? "PASS" : "FAIL");
  return ok;
}
