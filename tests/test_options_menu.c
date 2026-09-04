#include "guest_heap.h"
#include "options_menu.h"
#include "settings_overlay_state.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  HEAP_BASE = 0x31000000u,
  HEAP_SIZE = 0x00010000u,
  REGISTER_TARGET = 0x52001000u,
  CALLBACK_TARGET = 0x53001000u
};

void options_menu_stubs_set_manager(uint32_t value);
int options_menu_stubs_original_calls(void);
int options_menu_stubs_singleton_calls(void);
int options_menu_stubs_registration_calls(void);
const char *options_menu_stubs_command(void);
uint32_t options_menu_stubs_callback(void);
uint32_t options_menu_stubs_method(void);
x86_override_fn options_menu_stubs_callback_function(void);
int options_menu_stubs_override_is(const char *name, uint32_t ep);

static int check(int condition, const char *what) {
  if (condition)
    return 0;
  fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

int main(void) {
  CPU C;
  uint32_t manager, vtable, stack;
  int failures = 0;

  if (guest_heap_init(HEAP_BASE, HEAP_SIZE) != 0) {
    fprintf(stderr, "FAIL: could not create isolated guest heap\n");
    return 1;
  }
  manager = guest_malloc(8u);
  vtable = guest_malloc(0x20u);
  stack = guest_malloc(0x1000u);
  if (!manager || !vtable || !stack) {
    fprintf(stderr, "FAIL: isolated guest heap allocation failed\n");
    return 1;
  }
  WR32(manager, vtable);
  WR32(vtable + 0x10u, REGISTER_TARGET);
  options_menu_stubs_set_manager(manager);

  memset(&C, 0, sizeof C);
  C.reg[kX86pEsp] = stack + 0xff0u;
  WR32(C.reg[kX86pEsp], 0xabcdef01u);
  x2_override_005f4900(&C);

  failures += check(options_menu_stubs_override_is("XMen2.exe", 0x005f4900u),
                    "the additive command registrar override is absent");
  failures += check(!options_menu_stubs_override_is("XMen2.exe", 0x005f1c50u),
                    "the retail `options` callback was hijacked");
  failures += check(!options_menu_stubs_override_is("XMen2.exe", 0x005f1fa0u),
                    "the retail `options_main` callback was hijacked");
  failures += check(options_menu_stubs_original_calls() == 1,
                    "the retail registrar was not super-called exactly once");
  failures += check(options_menu_stubs_singleton_calls() == 1,
                    "the retail command registry was not obtained once");
  failures += check(options_menu_stubs_registration_calls() == 1,
                    "the port command was not registered exactly once");
  failures += check(!strcmp(options_menu_stubs_command(), "port_settings"),
                    "the registered command name is not `port_settings`");
  failures += check(options_menu_stubs_callback() == CALLBACK_TARGET,
                    "the registry received the wrong native callback");
  failures += check(options_menu_stubs_method() == REGISTER_TARGET,
                    "registration bypassed the retail vtable method");
  failures += check(C.reg[kX86pEsp] == stack + 0xff4u,
                    "the registrar did not preserve the retail RET result");
  failures +=
      check(C.reg[kX86pEax] == 0x12345678u && C.reg[kX86pEcx] == 0x23456789u &&
                C.reg[kX86pEdx] == 0x3456789au,
            "native registration changed the retail result registers");

  memset(&C, 0, sizeof C);
  C.reg[kX86pEsp] = stack + 0xfd0u;
  WR32(C.reg[kX86pEsp], 0xabcdef03u);
  x2_override_005f4900(&C);
  failures += check(options_menu_stubs_original_calls() == 2,
                    "a repeated retail registrar call was not super-called");
  failures += check(options_menu_stubs_registration_calls() == 1 &&
                        options_menu_stubs_singleton_calls() == 1,
                    "a repeated registrar duplicated the port command");

  x2_settings_overlay_hide();
  memset(&C, 0, sizeof C);
  C.reg[kX86pEax] = 0x76543210u;
  C.reg[kX86pEsp] = stack + 0xfe0u;
  WR32(C.reg[kX86pEsp], 0xabcdef02u);
  options_menu_stubs_callback_function()(&C);
  failures += check(x2_settings_overlay_visible(),
                    "the `port_settings` callback did not show its UI");
  failures += check(C.reg[kX86pEsp] == stack + 0xfe4u,
                    "the `port_settings` callback did not reproduce RET");
  failures += check(C.reg[kX86pEax] == 0x76543210u,
                    "the void port callback invented a return value");
  x2_settings_overlay_hide();
  failures += check(!x2_settings_overlay_visible(),
                    "closing Port Settings did not release guest input");

  printf("options menu ownership: %d of 17 checks passed\n", 17 - failures);
  return failures != 0;
}
