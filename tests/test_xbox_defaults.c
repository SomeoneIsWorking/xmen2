#include "xbox_defaults.h"
#include "controller_defaults_ui.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define SIZE 0x00700000u
#define CONTROLLER0_RVA 0x00668f40u
#define SETTER_RVA 0x002297a0u

static uint32_t mapped_base;
static X86Module module = { .name = "XMen2", .base = &mapped_base,
                            .preferred = 0x00400000u, .size = SIZE };
static int connected = -1, real_calls, setter_calls, callback_real_calls;
static int localization_real_calls;
static uint32_t heap_next;

X86Module *x86_modules(void) { return &module; }
int dinput_pad_count(void) { return connected < 0 ? 0 : 1; }
const char *dinput_pad_name(int pad)
{
    return pad == connected ? "test pad" : NULL;
}
int x86_peek(uint32_t addr, void *out, size_t n)
{
    if (addr < mapped_base || (uint64_t)addr + n > (uint64_t)mapped_base + SIZE)
        return 0;
    memcpy(out, (void *)(uintptr_t)addr, n);
    return 1;
}
int x86_peek32(uint32_t addr, uint32_t *out)
{
    return x86_peek(addr, out, sizeof *out);
}
void x86_guest_call_args(CPU *c, uint32_t target, uint32_t pop)
{
    uint32_t row, slot, kind, code, at;
    if (target != mapped_base + SETTER_RVA || pop != 16u) abort();
    row = RD32(c->esp); slot = RD32(c->esp + 4u);
    kind = RD32(c->esp + 8u); code = RD32(c->esp + 12u);
    if (row >= 42u || slot >= 4u) abort();
    at = c->ecx + 4u + (row * 4u + slot) * 12u;
    WR32(at + 4u, kind); WR32(at + 8u, code); WR32(at + 0u, 0u);
    setter_calls++;
}
void __real_fn_XMen2_0061b030(CPU *c)
{
    real_calls++;
    c->esp += 4u;
}
void x86_seg_unset(const char *seg) { (void)seg; abort(); }
__thread uint32_t g_fsbase, g_gsbase;

uint32_t guest_malloc(uint32_t bytes)
{
    uint32_t out = heap_next;
    heap_next += (bytes + 15u) & ~15u;
    if (heap_next > mapped_base + SIZE) abort();
    return out;
}
void guest_free(uint32_t address) { (void)address; }

void __wrap_fn_XMen2_0061b030(CPU *cpu);
void __wrap_fn_XMen2_006188c0(CPU *cpu);
void __wrap_fn_XMen2_00629ba0(CPU *cpu);

void __real_fn_XMen2_006188c0(CPU *cpu)
{
    callback_real_calls++;
    cpu->esp += 4u;
}

void __real_fn_XMen2_00629ba0(CPU *cpu)
{
    localization_real_calls++;
    cpu->eax = RD32(cpu->esp + 8u);
    cpu->esp += 4u;
}

static uint32_t slot_value(uint32_t object, int row, int field)
{
    return RD32(object + 4u + (uint32_t)(row * 4 + 2) * 12u + (uint32_t)field);
}

static uint32_t put_string(const char *text)
{
    uint32_t p = guest_malloc((uint32_t)strlen(text) + 1u);
    strcpy((char *)(uintptr_t)p, text);
    return p;
}

static uint32_t call_localize(CPU *cpu, uint32_t fallback)
{
    uint32_t top = cpu->esp;
    WR32(top, 0xfeed0101u);
    WR32(top + 4u, 0u);
    WR32(top + 8u, fallback);
    __wrap_fn_XMen2_00629ba0(cpu);
    if (cpu->esp != top + 4u) abort();
    cpu->esp = top;
    return cpu->eax;
}

static void call_default(CPU *cpu, uint32_t event, uint32_t preset)
{
    uint32_t top = cpu->esp;
    WR32(top, 0xfeed0202u);
    WR32(top + 4u, 0u);
    WR32(top + 8u, event);
    WR32(top + 12u, preset);
    __wrap_fn_XMen2_006188c0(cpu);
    if (cpu->esp != top + 4u) abort();
    cpu->esp = top;
}

int main(void)
{
    CPU cpu = {0};
    size_t n, i;
    const XboxDefaultBinding *defaults;
    uint32_t controller, object, stack;
    void *region = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (region == MAP_FAILED || (uintptr_t)region > UINT32_MAX) return 77;
    mapped_base = (uint32_t)(uintptr_t)region;
    heap_next = mapped_base + 0x00600000u;
    controller = mapped_base + 0x1000u;
    object = controller + 0x18u;
    stack = mapped_base + 0x5000u;
    WR32(mapped_base + CONTROLLER0_RVA, controller);
    WR32(object, 0u);
    WR32(stack, 0xfeedfaceu);
    cpu.esp = stack;
    connected = 0;

    __wrap_fn_XMen2_0061b030(&cpu);
    defaults = xbox_default_bindings(&n);
    if (real_calls != 1 || cpu.esp != stack + 4u || n != 21u ||
        setter_calls != (int)n) return 1;
    for (i = 0; i < n; i++)
        if (slot_value(object, defaults[i].binding, 4) != 3u ||
            slot_value(object, defaults[i].binding, 8) != defaults[i].code)
            return 2;
    if (slot_value(object, 10, 4) != 0u) return 3;

    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)n) return 4; /* idempotent */
    connected = -1;
    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)(2u * n)) return 5;
    for (i = 0; i < n; i++)
        if (slot_value(object, defaults[i].binding, 4) != 0u) return 6;

    /* A single existing pad mapping is user state: do not partially merge a
       preset around it and silently change the rest of the controls. */
    WR32(object + 4u + (4u * 4u + 2u) * 12u + 4u, 4u);
    WR32(object + 4u + (4u * 4u + 2u) * 12u + 8u, 0x31u);
    connected = 1;
    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)(2u * n) || slot_value(object, 4, 4) != 4u ||
        slot_value(object, 4, 8) != 0x31u) return 7;

    /* The UI's exact labels are native guest strings, while every unrelated
       localization request still runs through the retained function. */
    cpu.esp = stack;
    if (strcmp((char *)(uintptr_t)call_localize(&cpu, put_string("Defaults 1")),
               "Keyboard Defaults") != 0) return 8;
    if (strcmp((char *)(uintptr_t)call_localize(&cpu, put_string("Defaults 2")),
               "Xbox Defaults") != 0) return 9;
    {
        uint32_t other = put_string("Defaults 3");
        if (call_localize(&cpu, other) != other || localization_real_calls != 1)
            return 10;
    }

    /* Context 1 activation replaces the whole pad slot, including a custom
       row, because the user explicitly selected Xbox Defaults. It becomes
       persisted user state, so a later disconnect does not remove it. */
    call_default(&cpu, 2u, 1u);
    if (setter_calls != (int)(3u * n + 1u) ||
        slot_value(object, 4, 4) != 4u || slot_value(object, 4, 8) != 0x15u ||
        slot_value(object, 12, 8) != 0x14u ||
        slot_value(object, 13, 8) != 0x13u ||
        slot_value(object, 14, 8) != 0x12u ||
        slot_value(object, 15, 8) != 0x11u) return 11;
    connected = -1;
    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)(3u * n + 1u) ||
        slot_value(object, 4, 4) != 4u) return 12;

    /* Keyboard Defaults and non-activation events remain the shipped body. */
    call_default(&cpu, 2u, 0u);
    call_default(&cpu, 1u, 1u);
    if (callback_real_calls != 2) return 13;

    printf("xbox defaults: %zu exact bindable rows; UI labels/actions and ownership ok\n", n);
    return 0;
}
