#include "options_menu.h"
#include "guest_memory.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EXE_BASE = 0x41000000u,
    COMMAND_REGISTRY_RVA = 0x0015c890u,
    REGISTER_TARGET = 0x52001000u,
    CALLBACK_TARGET = 0x53001000u
};

static uint32_t mapped_exe = EXE_BASE;
static X86Module module = {
    .name = "XMen2.exe",
    .base = &mapped_exe,
    .preferred = 0x00400000u
};
static uint32_t manager;
static int original_calls, singleton_calls, registration_calls;
static char registered_name[32];
static uint32_t registered_callback, registered_method;
static x86_override_fn callback_function;
static struct {
    const char *module;
    uint32_t ep;
    x86_override_fn fn;
} overrides[8];
static int override_count;

volatile uint32_t g_sample_ep;
uint32_t g_guest_watch_addr;
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value)
{
    fprintf(stderr,
            "options menu stub: unexpected write watch 0x%08x = 0x%08x\n",
            address, value);
    abort();
}

void options_menu_stubs_set_manager(uint32_t value)
{
    manager = value;
}

int options_menu_stubs_original_calls(void) { return original_calls; }
int options_menu_stubs_singleton_calls(void) { return singleton_calls; }
int options_menu_stubs_registration_calls(void) { return registration_calls; }
const char *options_menu_stubs_command(void) { return registered_name; }
uint32_t options_menu_stubs_callback(void) { return registered_callback; }
uint32_t options_menu_stubs_method(void) { return registered_method; }
x86_override_fn options_menu_stubs_callback_function(void)
{
    return callback_function;
}

int options_menu_stubs_override_is(const char *name, uint32_t ep)
{
    int index;
    for (index = 0; index < override_count; ++index)
        if (!strcmp(overrides[index].module, name) &&
            overrides[index].ep == ep)
            return 1;
    return 0;
}

void fn_XMen2_005f4900(CPU *C)
{
    original_calls++;
    C->eax = 0x12345678u;
    C->ecx = 0x23456789u;
    C->edx = 0x3456789au;
    C->esp += 4u;
}

X86Module *x86_modules(void)
{
    return &module;
}

uint32_t x86_native_callback(x86_override_fn fn, const char *owner,
                             const char *name, void *ctx)
{
    if (strcmp(owner, "options_menu") || strcmp(name, "port_settings") || ctx) {
        fprintf(stderr, "options menu stub: callback identity changed\n");
        abort();
    }
    callback_function = fn;
    return CALLBACK_TARGET;
}

void x86_guest_call_args(CPU *C, uint32_t target, uint32_t callee_pop_bytes)
{
    if (target == EXE_BASE + COMMAND_REGISTRY_RVA) {
        if (callee_pop_bytes) abort();
        singleton_calls++;
        C->eax = manager;
        return;
    }
    if (target == REGISTER_TARGET) {
        const char *name = guest_memory_const_pointer(RD32(C->esp));
        if (callee_pop_bytes != 8u) abort();
        registration_calls++;
        registered_method = target;
        registered_callback = RD32(C->esp + 4u);
        snprintf(registered_name, sizeof registered_name, "%s", name);
        C->esp += 8u;
        C->eax = 1u;
        return;
    }
    fprintf(stderr, "options menu stub: unexpected guest target 0x%08x\n",
            target);
    abort();
}

void x86_register_override(const char *name, uint32_t ep,
                           x86_override_fn fn)
{
    if (override_count == (int)(sizeof overrides / sizeof overrides[0]))
        abort();
    overrides[override_count].module = name;
    overrides[override_count].ep = ep;
    overrides[override_count].fn = fn;
    override_count++;
}

void x86_diag_dump(void)
{
    fprintf(stderr, "options menu stub: unexpected guest-heap diagnostic\n");
}
