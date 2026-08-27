/*
 * Reconcile the port's configured output mode at the retail settings-load
 * boundary. A fresh profile takes XMen2.exe 0x00619770's default branch and
 * persists 800x600 after the host's early publication; a warm profile reads
 * the published value. Preserve that complete body so every other first-run
 * default and Version=7 write remains retail-owned, then republish and invoke
 * the retail Resolution reader itself before device creation.
 */
#include "display_mode_seed.h"
#include "guest_memory.h"
#include "settings_store.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    LINKED_SETTINGS_LOAD = 0x00619770u,
    EXE_PREFERRED_BASE = 0x00400000u,
    RVA_BUILD_REGISTRY_CONTEXT = 0x00216a70u,
    RVA_READ_STRING = 0x00216e10u,
    RVA_PUBLISHER_NAME = 0x002a3a64u,
    RVA_PRODUCT_NAME = 0x002a3a70u,
    RVA_DEMO_PRODUCT_NAME = 0x002a3a80u,
    RVA_RESOLUTION_PATH = 0x002a4dccu,
    RVA_RESOLUTION_DEFAULT = 0x002a4e40u,
    RVA_RESOLUTION_OUTPUT = 0x00668d9cu,
    RVA_DEMO_FLAG = 0x002f3c2du,
    REGISTRY_CONTEXT_BYTES = 0x78u,
    RESOLUTION_CAPACITY = 10u
};

void fn_XMen2_00619770(CPU *C);

static void refuse(const char *reason, const char *expected,
                   const char *actual)
{
    fprintf(stderr, "DISPLAY RUNTIME: REFUSED: %s; expected '%s', got '%s'.\n",
            reason, expected ? expected : "?", actual ? actual : "?");
    fflush(stderr);
    abort();
}

static uint32_t executable_base(void)
{
    X86Module *module;

    for (module = x86_modules(); module; module = module->next) {
        if (!strcmp(module->name, "XMen2.exe")) {
            if (!module->base || !*module->base
                || module->preferred != EXE_PREFERRED_BASE)
                return 0u;
            return *module->base;
        }
    }
    return 0u;
}

static uint32_t build_registry_context(const CPU *source, uint32_t exe)
{
    CPU call = *source;
    uint32_t context = call.esp - REGISTRY_CONTEXT_BYTES;
    uint32_t product = RD8(exe + RVA_DEMO_FLAG)
        ? exe + RVA_DEMO_PRODUCT_NAME : exe + RVA_PRODUCT_NAME;

    memset(guest_memory_pointer(context), 0, REGISTRY_CONTEXT_BYTES);
    call.esp = context;
    call.esp -= 4u; WR32(call.esp, product);
    call.esp -= 4u; WR32(call.esp, exe + RVA_PUBLISHER_NAME);
    call.ecx = context;
    x86_guest_call_args(&call, exe + RVA_BUILD_REGISTRY_CONTEXT, 8u);
    if (call.eax != context)
        refuse("retail registry context constructor returned another object",
               NULL, NULL);
    return context;
}

static void reread_resolution(const CPU *source, uint32_t exe,
                              const char *expected)
{
    CPU call = *source;
    const char *actual;
    uint32_t context = build_registry_context(source, exe);

    /* Keep the reader's 0x314-byte local frame below the context, matching
       the original loader where the context lives in its caller's frame. */
    call.esp = context;
    call.esp -= 4u; WR32(call.esp, RESOLUTION_CAPACITY);
    call.esp -= 4u; WR32(call.esp, exe + RVA_RESOLUTION_OUTPUT);
    call.esp -= 4u; WR32(call.esp, exe + RVA_RESOLUTION_DEFAULT);
    call.esp -= 4u; WR32(call.esp, exe + RVA_RESOLUTION_PATH);
    call.ecx = context;
    x86_guest_call_args(&call, exe + RVA_READ_STRING, 16u);

    actual = guest_memory_const_pointer(exe + RVA_RESOLUTION_OUTPUT);
    if (!actual || strcmp(actual, expected) != 0)
        refuse("retail Resolution reader did not produce the configured mode",
               expected, actual);
}

static void x2_override_display_settings_load(CPU *C)
{
    const X2Settings *settings;
    uint32_t exe;
    char expected[32];

    fn_XMen2_00619770(C);

    settings = x2_settings_store();
    if (!x2_display_mode_seed_format(settings->width, settings->height,
                                     expected, (int)sizeof expected)
        || strlen(expected) >= RESOLUTION_CAPACITY)
        refuse("configured mode exceeds the retail string capacity",
               expected, NULL);

    (void)x2_display_mode_seed_publish();
    if (!x2_display_mode_seed_is_current())
        refuse("configured mode could not be republished after first-run defaults",
               expected, NULL);

    exe = executable_base();
    if (!exe) refuse("XMen2.exe mapping is unavailable", expected, NULL);
    reread_resolution(C, exe, expected);
    fprintf(stderr, "DISPLAY RUNTIME: retail settings resolved configured "
                    "mode %s after first-run/default initialization.\n",
            expected);
}

__attribute__((constructor))
static void x2_display_mode_runtime_register_override(void)
{
    x86_register_override("XMen2.exe", LINKED_SETTINGS_LOAD,
                          x2_override_display_settings_load);
}
