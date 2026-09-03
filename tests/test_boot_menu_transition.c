#include "boot_menu_transition.h"
#include "x86rt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static CPU captured;
static uint32_t captured_target;
static uint32_t captured_pop;
static int calls;
static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

void x86_guest_call_args(CPU *cpu, uint32_t target, uint32_t pop) {
  captured = *cpu;
  captured_target = target;
  captured_pop = pop;
  calls++;
}

int main(void) {
  CPU source;
  memset(&source, 0, sizeof source);
  source.eax = 0x12345678u;
  source.ecx = 0x23456789u;
  source.esp = 0x00102000u;

  CHECK(!x2_boot_menu_open(NULL, 0x08000000u));
  CHECK(!x2_boot_menu_open(&source, 0));
  CHECK(calls == 0);
  CHECK(x2_boot_menu_open(&source, 0x08000000u));
  CHECK(calls == 1);
  /* 0x0049fb00 executes `mainmenuexit 1`; unlike the adjacent no-argument
     callback, the retail handler takes its direct reset/load-menu branch. */
  CHECK(captured_target == 0x0809fb00u);
  CHECK(captured_pop == 0u);
  CHECK(captured.eax == source.eax && captured.ecx == source.ecx &&
        captured.esp == source.esp);

  printf("boot_menu_transition: %d checks passed\n", checks);
  return 0;
}
