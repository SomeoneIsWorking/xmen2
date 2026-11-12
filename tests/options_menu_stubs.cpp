#include "friend_code_rows.hpp"
#include "guest_heap.h"
#include "guest_memory.h"
#include "options_menu.h"
#include "port_option_rows.hpp"
#include "retail_front_end.hpp"
#include "retail_menu_model.hpp"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  EXE_BASE = 0x41000000u,
  COMMAND_REGISTRY_RVA = 0x0015c890u,
  REGISTER_TARGET = 0x52001000u,
  JOIN_CALLBACK_TARGET = 0x53001010u,
  FRIEND_CALLBACK_TARGET = 0x53001030u,
  ROW_CALLBACK_TARGET = 0x53001020u,
  MAX_REGISTRATIONS = 8
};

static uint32_t mapped_exe = EXE_BASE;
static X86Module module = {
    .name = "XMen2.exe", .base = &mapped_exe, .preferred = 0x00400000u};
static uint32_t manager;
static int original_calls, singleton_calls, registration_calls;
static char registered_name[MAX_REGISTRATIONS][32];
static uint32_t registered_callback[MAX_REGISTRATIONS], registered_method;
static int join_commands;
static x86_override_fn row_function;
static void *row_ctx[2];
static int row_callbacks;
static void *current_ctx;
static const x2::native::PortOptionRow *requested_row;
static int requested_direction;
static struct {
  const char *module;
  uint32_t ep;
  x86_override_fn fn;
} overrides[8];
static int override_count;

volatile uint32_t g_sample_ep;
uint32_t g_guest_watch_addr;
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value) {
  fprintf(stderr, "options menu stub: unexpected write watch 0x%08x = 0x%08x\n",
          address, value);
  abort();
}

void options_menu_stubs_set_manager(uint32_t value) { manager = value; }

int options_menu_stubs_original_calls(void) { return original_calls; }
int options_menu_stubs_singleton_calls(void) { return singleton_calls; }
int options_menu_stubs_registration_calls(void) { return registration_calls; }
const char *options_menu_stubs_command(int index) {
  return registered_name[index];
}
uint32_t options_menu_stubs_callback(int index) {
  return registered_callback[index];
}
int options_menu_stubs_join_commands(void) { return join_commands; }

/* lan_session.cpp's command, port_option_rows.cpp's rows and the live
   menu reads, which this test does not link: the menu up has the
   Resolution row focused. */
static const x2::native::PortOptionRow kStubRow = {
    "resolution", "Resolution", false, x2::native::PortPage::Options};
static uint32_t stub_menu, stub_focus;
uint32_t x2::retail::FrontEnd::active_menu_object(const CPU &) {
  if (!stub_menu) {
    stub_menu = guest_malloc(0x400u);
    stub_focus = guest_malloc(0x10u);
  }
  WR32(stub_menu + x2::menu::kMenuFocus, stub_focus);
  return stub_menu;
}
std::string x2::menu::read_live_item_name(std::uint32_t item) {
  return item != 0u && item == stub_focus ? "port_option_resolution" : "";
}
namespace x2::native {
const PortOptionRow *port_option_row_of_item(std::string_view item) {
  return item == "port_option_resolution" ? &kStubRow : nullptr;
}
void port_option_choose(const CPU &, const PortOptionRow &row, int direction) {
  requested_row = &row;
  requested_direction = direction;
}
void lan_join_command(CPU *C) {
  join_commands++;
  C->reg[kX86pEsp] += 4u;
}
void friend_row_command(CPU *C) { C->reg[kX86pEsp] += 4u; }
} // namespace x2::native
uint32_t options_menu_stubs_method(void) { return registered_method; }
/* Runs the port row command registered `index`-th (0 next, 1 previous) and
   reports the step it requested. */
int options_menu_stubs_run_row(int index, CPU *C) {
  requested_row = nullptr;
  current_ctx = row_ctx[index];
  row_function(C);
  current_ctx = nullptr;
  return requested_row == &kStubRow ? requested_direction : 0;
}
void *x86_callback_ctx(void) { return current_ctx; }

int options_menu_stubs_override_is(const char *name, uint32_t ep) {
  int index;
  for (index = 0; index < override_count; ++index)
    if (!strcmp(overrides[index].module, name) && overrides[index].ep == ep)
      return 1;
  return 0;
}

static void guest_body_005f4900(CPU *C) {
  original_calls++;
  C->reg[kX86pEax] = 0x12345678u;
  C->reg[kX86pEcx] = 0x23456789u;
  C->reg[kX86pEdx] = 0x3456789au;
  C->reg[kX86pEsp] += 4u;
}

X86Module *x86_modules(void) { return &module; }

uint32_t x86_native_callback(x86_override_fn fn, const char *owner,
                             const char *name, void *ctx) {
  if (!ctx && !strcmp(owner, "lan_session") && !strcmp(name, "port_lan_join") &&
      fn == x2::native::lan_join_command) {
    return JOIN_CALLBACK_TARGET;
  }
  if (!ctx && !strcmp(owner, "friend_code_rows") &&
      !strcmp(name, "port_friend_row") &&
      fn == x2::native::friend_row_command) {
    return FRIEND_CALLBACK_TARGET;
  }
  if (ctx && !strcmp(owner, "port_option_rows") && row_callbacks < 2) {
    row_function = fn;
    row_ctx[row_callbacks++] = ctx;
    return ROW_CALLBACK_TARGET;
  }
  fprintf(stderr, "options menu stub: callback identity changed\n");
  abort();
}

void x86_guest_call_args(CPU *C, uint32_t target, uint32_t callee_pop_bytes) {
  if (target == EXE_BASE + COMMAND_REGISTRY_RVA) {
    if (callee_pop_bytes)
      abort();
    singleton_calls++;
    C->reg[kX86pEax] = manager;
    return;
  }
  if (target == REGISTER_TARGET) {
    const char *name = guest_memory_as<const char>(RD32(C->reg[kX86pEsp]));
    if (callee_pop_bytes != 8u || registration_calls == MAX_REGISTRATIONS)
      abort();
    registered_method = target;
    registered_callback[registration_calls] = RD32(C->reg[kX86pEsp] + 4u);
    snprintf(registered_name[registration_calls],
             sizeof registered_name[registration_calls], "%s", name);
    registration_calls++;
    C->reg[kX86pEsp] += 8u;
    C->reg[kX86pEax] = 1u;
    return;
  }
  fprintf(stderr, "options menu stub: unexpected guest target 0x%08x\n",
          target);
  abort();
}

void x86_register_override(const char *name, uint32_t ep, x86_override_fn fn) {
  if (override_count == (int)(sizeof overrides / sizeof overrides[0]))
    abort();
  overrides[override_count].module = name;
  overrides[override_count].ep = ep;
  overrides[override_count].fn = fn;
  override_count++;
}

void x86_diag_dump(void) {
  fprintf(stderr, "options menu stub: unexpected guest-heap diagnostic\n");
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  if (linked_ep == 0x005f4900u && !strcmp(module, "XMen2.exe")) {
    guest_body_005f4900(C);
    return;
  }
  fprintf(stderr,
          "%s: x86_guest_body(%s, 0x%08x) is not modelled by this test.\n",
          "options_menu_stubs.cpp", module, linked_ep);
  abort();
}
