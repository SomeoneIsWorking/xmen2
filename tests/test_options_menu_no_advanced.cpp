/*
 * CMenuOptions' update override while the menu waits: it must hand the menu
 * manager's wait (+0x74) the retail arguments (0, 0). Every host->guest call
 * pushes a 0xDEADBEEF return address below the caller's ESP, so a call made
 * with the override's own ESP after the arguments were pushed lands on the
 * second argument -- which the wait then hands the console as a command string
 * (0x005db2a5), the SIGSEGV at guest 0xdeadbeef seen on Apply in a level.
 */
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
  HEAP_BASE = 0x31000000u,
  HEAP_SIZE = 0x00010000u,
  EXE_BASE = 0x00400000u,
  GAME_STATE_GET = 0x004b2880u,
  PROMPT_GET = 0x005eb300u,
  MENU_MGR_GET = 0x005d8920u,
  OPTIONS_UPDATE = 0x005cbdf0u,
  NOT_BUSY = 0x52000010u,
  MANAGER_WAIT = 0x52000020u,
  RETURN_ADDRESS = 0xabcdef01u
};

static uint32_t mapped_exe = EXE_BASE;
static X86Module module = {
    .name = "XMen2.exe", .base = &mapped_exe, .preferred = EXE_BASE};
static x86_override_fn update_override;
static uint32_t game_state, prompt, manager;
static uint32_t wait_args[2];
static int wait_calls;

volatile uint32_t g_sample_ep;
uint32_t g_guest_watch_addr;
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t, uint32_t) { abort(); }
void x86_diag_dump(void) {}
X86Module *x86_modules(void) { return &module; }

void x86_register_override(const char *, uint32_t ep, x86_override_fn fn) {
  if (ep == OPTIONS_UPDATE)
    update_override = fn;
}

void x86_guest_body(CPU *, const char *, uint32_t linked_ep) {
  fprintf(stderr, "unexpected retail body 0x%08x\n", linked_ep);
  abort();
}

void x86_guest_body_resume(CPU *, const char *, uint32_t linked_at, uint32_t) {
  fprintf(stderr, "unexpected resume at 0x%08x\n", linked_at);
  abort();
}

/* The production contract: the return address goes below ESP, the callee
   pops it and its own argument bytes. */
void x86_guest_call_args(CPU *C, uint32_t target, uint32_t callee_pop_bytes) {
  C->reg[kX86pEsp] -= 4u;
  WR32(C->reg[kX86pEsp], 0xdeadbeefu);
  const uint32_t esp = C->reg[kX86pEsp];
  if (target == GAME_STATE_GET)
    C->reg[kX86pEax] = game_state;
  else if (target == PROMPT_GET)
    C->reg[kX86pEax] = prompt;
  else if (target == MENU_MGR_GET)
    C->reg[kX86pEax] = manager;
  else if (target == NOT_BUSY)
    C->reg[kX86pEax] = 0u;
  else if (target == MANAGER_WAIT) {
    wait_calls++;
    wait_args[0] = RD32(esp + 4u);
    wait_args[1] = RD32(esp + 8u);
    C->reg[kX86pEax] = 1u;
  } else {
    fprintf(stderr, "unexpected guest target 0x%08x\n", target);
    abort();
  }
  C->reg[kX86pEsp] = esp + 4u + callee_pop_bytes;
}

static uint32_t object_with(uint32_t slot, uint32_t target) {
  const uint32_t object = guest_malloc(8u);
  const uint32_t vtable = guest_malloc(0x100u);
  WR32(object, vtable);
  WR32(vtable + slot, target);
  return object;
}

static int check(bool condition, const char *what) {
  if (condition)
    return 0;
  fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

int main(void) {
  if (guest_heap_init(HEAP_BASE, HEAP_SIZE) != 0 || !update_override) {
    fprintf(stderr, "FAIL: setup\n");
    return 1;
  }
  game_state = object_with(0x18u, NOT_BUSY);
  prompt = object_with(0x78u, NOT_BUSY);
  manager = object_with(0x74u, MANAGER_WAIT);
  const uint32_t menu = guest_malloc(0x1900u);
  const uint32_t stack = guest_malloc(0x1000u);
  WR8(menu + 0x18e4u, 1u);

  CPU C;
  memset(&C, 0, sizeof C);
  C.reg[kX86pEcx] = menu;
  C.reg[kX86pEsp] = stack + 0xff0u;
  WR32(C.reg[kX86pEsp], RETURN_ADDRESS);
  const uint32_t entry_esp = C.reg[kX86pEsp];
  update_override(&C);

  int failures = 0;
  failures += check(wait_calls == 1, "the manager's wait did not run once");
  failures += check(wait_args[0] == 0u && wait_args[1] == 0u,
                    "the manager's wait did not receive (0, 0)");
  failures += check(C.reg[kX86pEsp] == entry_esp + 4u,
                    "the update did not return past its return address");
  failures += check(C.reg[kX86pEax] == 1u,
                    "the update did not return the wait's result");
  if (!failures)
    printf("options_menu_no_advanced: ok\n");
  return failures ? 1 : 0;
}
