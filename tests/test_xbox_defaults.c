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
#define CONTROLLERS 16
#define SETS 3                 /* master, working copy, menu copy */
#define ALT_SLOT 1u

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
/*
 * FUN_006297a0, modelled from its own arithmetic rather than from memory.
 *
 * `EAX = slot + row*4; EAX *= 3; element = this + EAX*4`, then `[element+4] =
 * kind`, `[element+8] = code`, `[element+0xc] = 0`. The array starts AT the
 * object with no leading field; an earlier version of this stub inserted one,
 * which made the shipped code and this test agree on a layout the game does
 * not use, so neither could see the mistake.
 */
void x86_guest_call_args(CPU *c, uint32_t target, uint32_t pop)
{
    uint32_t row, slot, kind, code, at;
    if (target != mapped_base + SETTER_RVA || pop != 16u) abort();
    row = RD32(c->esp); slot = RD32(c->esp + 4u);
    kind = RD32(c->esp + 8u); code = RD32(c->esp + 12u);
    if (row >= 42u || slot >= 4u) abort();
    at = c->ecx + (row * 4u + slot) * 12u;
    WR32(at + 4u, kind); WR32(at + 8u, code); WR32(at + 0xcu, 0u);
    setter_calls++;
}
void fn_XMen2_0061b030(CPU *c)
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

void x2_override_0061b030(CPU *cpu);
void x2_override_006188c0(CPU *cpu);
void x2_override_00629ba0(CPU *cpu);

void fn_XMen2_006188c0(CPU *cpu)
{
    callback_real_calls++;
    cpu->esp += 4u;
}

void fn_XMen2_00629ba0(CPU *cpu)
{
    localization_real_calls++;
    cpu->eax = RD32(cpu->esp + 8u);
    cpu->esp += 4u;
}

static uint32_t g_object[CONTROLLERS];

static uint32_t slot_at(int set, int row, unsigned slot, int field)
{
    return RD32(g_object[set] + (uint32_t)(row * 4 + (int)slot) * 12u +
                (uint32_t)field);
}

/* The preset's own slot, in the master set. */
static uint32_t slot_value(uint32_t object, int row, int field)
{
    return RD32(object + (uint32_t)(row * 4 + (int)ALT_SLOT) * 12u +
                (uint32_t)field);
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
    x2_override_00629ba0(cpu);
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
    x2_override_006188c0(cpu);
    if (cpu->esp != top + 4u) abort();
    cpu->esp = top;
}

int main(void)
{
    CPU cpu = {0};
    size_t n, i;
    const XboxDefaultBinding *defaults;
    uint32_t controller, object, stack;
    int set;
    void *region = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (region == MAP_FAILED || (uintptr_t)region > UINT32_MAX) return 77;
    mapped_base = (uint32_t)(uintptr_t)region;
    heap_next = mapped_base + 0x00600000u;
    /* Sixteen controllers, as FUN_0061b030 builds them: masters 0..3, the
       working copies 4..7 the game evaluates, 8..11, and the menu copies
       12..15. A preset that reaches only the master is invisible to the
       running game, so the test has to be able to see all of them. */
    stack = mapped_base + 0x5000u;
    for (set = 0; set < CONTROLLERS; set++) {
        controller = mapped_base + 0x10000u + (uint32_t)set * 0x2000u;
        g_object[set] = controller + 0x18u;
        WR32(mapped_base + CONTROLLER0_RVA + (uint32_t)set * 4u, controller);
        WR32(g_object[set], 0u);
    }
    controller = mapped_base + 0x10000u;
    object = g_object[0];
    WR32(stack, 0xfeedfaceu);
    cpu.esp = stack;
    connected = 0;

    x2_override_0061b030(&cpu);
    defaults = xbox_default_bindings(&n);
    if (real_calls != 1 || cpu.esp != stack + 4u || n != 21u ||
        setter_calls != (int)n * SETS) return 1;
    /* Every set player 0's bindings live in, not the master alone: the master
       is what the options UI edits, set 4 is what the game evaluates, set 12
       is what the menus use. Missing set 4 was issue #82's second half -- the
       preset was present, correct and never read. */
    for (i = 0; i < n; i++) {
        static const int PLAYER0_SETS[SETS] = { 0, 4, 12 };
        int k;
        for (k = 0; k < SETS; k++)
            if (slot_at(PLAYER0_SETS[k], defaults[i].binding, ALT_SLOT, 4)
                    != 3u ||
                slot_at(PLAYER0_SETS[k], defaults[i].binding, ALT_SLOT, 8)
                    != defaults[i].code)
                return 2;
    }
    if (slot_value(object, 10, 4) != 0u) return 3;
    /* Another player's sets are untouched: this preset owns player 1 only. */
    for (i = 0; i < n; i++)
        if (slot_at(1, defaults[i].binding, ALT_SLOT, 4) != 0u ||
            slot_at(5, defaults[i].binding, ALT_SLOT, 4) != 0u) return 14;
    /* And slots 2 and 3, which FUN_0061b030 rewrites with its own menu keys
       after copying the master, are never written by the preset. */
    for (i = 0; i < n; i++)
        if (slot_at(4, defaults[i].binding, 2u, 4) != 0u ||
            slot_at(4, defaults[i].binding, 3u, 4) != 0u) return 15;

    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)n * SETS) return 4; /* idempotent */
    connected = -1;
    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)(2u * n) * SETS) return 5;
    for (i = 0; i < n; i++)
        if (slot_value(object, defaults[i].binding, 4) != 0u ||
            slot_at(4, defaults[i].binding, ALT_SLOT, 4) != 0u) return 6;

    /* A single existing pad mapping is user state: do not partially merge a
       preset around it and silently change the rest of the controls. */
    WR32(object + (4u * 4u + ALT_SLOT) * 12u + 4u, 4u);
    WR32(object + (4u * 4u + ALT_SLOT) * 12u + 8u, 0x31u);
    connected = 1;
    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)(2u * n) * SETS || slot_value(object, 4, 4) != 4u ||
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
    if (setter_calls != (int)(3u * n + 1u) * SETS ||
        slot_value(object, 4, 4) != 4u || slot_value(object, 4, 8) != 0x15u ||
        slot_value(object, 12, 8) != 0x14u ||
        slot_value(object, 13, 8) != 0x13u ||
        slot_value(object, 14, 8) != 0x12u ||
        slot_value(object, 15, 8) != 0x11u) return 11;
    connected = -1;
    xbox_defaults_sync(&cpu);
    if (setter_calls != (int)(3u * n + 1u) * SETS ||
        slot_value(object, 4, 4) != 4u) return 12;

    /* Keyboard Defaults and non-activation events remain the shipped body. */
    call_default(&cpu, 2u, 0u);
    call_default(&cpu, 1u, 1u);
    if (callback_real_calls != 2) return 13;

    printf("xbox defaults: %zu exact bindable rows; UI labels/actions and ownership ok\n", n);
    return 0;
}
