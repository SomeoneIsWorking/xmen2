#include "dialog_prompts.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define EXE_BASE        0x00400000u
#define IMAGE_SIZE      0x00700000u
#define LOCALIZE_RETURN 0x005ec066u
#define TEXT_KEY        0x00685cbcu
#define EMPTY_TEXT      0x00681968u

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

static uint32_t mapped_base;
static X86Module module = { .name = "XMen2.exe", .base = &mapped_base,
                            .preferred = EXE_BASE, .size = IMAGE_SIZE };
static int player_uses_gamepad;
static int super_calls;
static int asset_calls;
static uint32_t expected_outer_esp;

X86Module *x86_modules(void) { return &module; }
int x2_player_input_uses_gamepad(unsigned player)
{
    CHECK(player == 0);
    return player_uses_gamepad;
}

void fn_XMen2_00629bf0(CPU *C)
{
    super_calls++;
    C->eax = 0x11112222u;
    C->esp += 4u;
}

void fn_XMen2_00564b70(CPU *C)
{
    asset_calls++;
    CHECK(C->ecx == expected_outer_esp + 0x1cu);
    CHECK(RD32(C->esp + 4u) == mapped_base + (TEXT_KEY - EXE_BASE));
    CHECK(RD32(C->esp + 8u) == mapped_base + (EMPTY_TEXT - EXE_BASE));
    C->eax = 0x33334444u;
    C->esp += 12u; /* RET 8 */
}

void x2_override_00629bf0(CPU *C);
int native_stubs_registered(const char *module_name, uint32_t linked_ep);

static uint32_t mapped(uint32_t linked)
{
    return mapped_base + (linked - EXE_BASE);
}

static void run_lookup(int pads, uint32_t linked_return, uint32_t want_eax,
                       int want_super, int want_asset)
{
    CPU C = {0};
    uint32_t stack = mapped_base + 0x2000u;
    int before_super = super_calls;
    int before_asset = asset_calls;

    player_uses_gamepad = pads;
    expected_outer_esp = stack;
    WR32(stack, mapped(linked_return));
    WR32(stack + 4u, mapped_base + 0x3000u);
    C.esp = stack;
    x2_override_00629bf0(&C);
    CHECK(C.esp == stack + 4u);
    CHECK(C.eax == want_eax);
    CHECK(super_calls - before_super == want_super);
    CHECK(asset_calls - before_asset == want_asset);
}

int main(void)
{
    int result;
    mapped_base = 0x10000000u;
    result = guest_memory_init(); CHECK(result == 0);
    result = guest_memory_map_fixed(mapped_base, IMAGE_SIZE,
                                    PROT_READ | PROT_WRITE);
    CHECK(result == 0);

    CHECK(native_stubs_registered("XMen2.exe", 0x00629bf0u));
    CHECK(!dialog_prompts_use_asset_text(0, LOCALIZE_RETURN));
    CHECK(!dialog_prompts_use_asset_text(1, 0x00629c05u));
    CHECK(dialog_prompts_use_asset_text(1, LOCALIZE_RETURN));

    run_lookup(0, LOCALIZE_RETURN, 0x11112222u, 1, 0);
    run_lookup(1, 0x00629c05u, 0x11112222u, 1, 0);
    run_lookup(1, LOCALIZE_RETURN, 0x33334444u, 0, 1);
    /* The choice is live per popup, not cached when a controller existed. */
    run_lookup(0, LOCALIZE_RETURN, 0x11112222u, 1, 0);

    result = guest_memory_release(mapped_base, IMAGE_SIZE);
    CHECK(result == 0);
    printf("dialog_prompts: %d checks passed\n", checks);
    return 0;
}
