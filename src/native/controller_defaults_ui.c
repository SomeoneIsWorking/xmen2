/* Native adapter for the PC controller editor's three retained preset buttons.
 *
 * FUN_0061dc10 builds the dialog and wires all three buttons to FUN_006188c0
 * with context 0, 1, or 2. The retained callback applies the three shipped
 * keyboard/mouse tables and intentionally edits only slots 0 and 1. Context 0
 * remains that implementation and is relabelled Keyboard Defaults. Context 1
 * becomes Xbox Defaults and delegates to xbox_defaults_apply; every other
 * event and context remains in the translated callback.
 */
#include "controller_defaults_ui.h"

#include "guest_heap.h"
#include "xbox_defaults.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_keyboard_label;
static uint32_t g_xbox_label;
static unsigned long g_keyboard_lookups, g_xbox_lookups;
static unsigned long g_xbox_actions, g_xbox_refusals;

static int guest_equals(uint32_t address, const char *expected)
{
    size_t i;
    uint8_t ch;
    if (!address) return 0;
    for (i = 0;; i++) {
        if (!x86_peek(address + (uint32_t)i, &ch, 1u)) return 0;
        if (ch != (uint8_t)expected[i]) return 0;
        if (!ch) return 1;
    }
}

static uint32_t guest_label(uint32_t *storage, const char *text)
{
    size_t bytes;
    if (*storage) return *storage;
    bytes = strlen(text) + 1u;
    *storage = guest_malloc((uint32_t)bytes);
    if (*storage) memcpy((void *)(uintptr_t)*storage, text, bytes);
    return *storage;
}

void __real_fn_XMen2_00629ba0(CPU *cpu);

void __wrap_fn_XMen2_00629ba0(CPU *cpu)
{
    uint32_t fallback = RD32(cpu->esp + 8u);
    if (guest_equals(fallback, "Defaults 1")) {
        cpu->eax = guest_label(&g_keyboard_label, "Keyboard Defaults");
        g_keyboard_lookups++;
        cpu->esp += 4u;
        return;
    }
    if (guest_equals(fallback, "Defaults 2")) {
        cpu->eax = guest_label(&g_xbox_label, "Xbox Defaults");
        g_xbox_lookups++;
        cpu->esp += 4u;
        return;
    }
    __real_fn_XMen2_00629ba0(cpu);
}

void __real_fn_XMen2_006188c0(CPU *cpu);

void __wrap_fn_XMen2_006188c0(CPU *cpu)
{
    uint32_t event = RD32(cpu->esp + 8u);
    uint32_t preset = RD32(cpu->esp + 12u);
    if (event == 2u && preset == 1u) {
        if (xbox_defaults_apply(cpu)) g_xbox_actions++;
        else g_xbox_refusals++;
        cpu->esp += 4u;
        return;
    }
    __real_fn_XMen2_006188c0(cpu);
}

void controller_defaults_ui_report(void)
{
    printf("  Controller defaults UI: Keyboard label %lu, Xbox label %lu; "
           "Xbox action %lu applied, %lu refused (no ready pad/bindings).\n",
           g_keyboard_lookups, g_xbox_lookups, g_xbox_actions,
           g_xbox_refusals);
}
