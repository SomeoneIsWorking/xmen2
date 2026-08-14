/*
 * Xbox-release controller defaults through XMen2.exe's own binding setter.
 *
 * The console evidence supplies semantics, while the PC executable supplies
 * the storage contract. Xbox options_controller_xbox labels A/B/X/Y as
 * Punch/Slam/Use/Jump, RT/LT as Power/Allies, the d-pad as hero control,
 * Back/Start as Team/Pause, and the right stick as camera + map click.
 * XMen2.exe FUN_0061b030 names the 42 binding rows; FUN_006281f0 proves the
 * signed-axis/POV/button codes; FUN_006297a0 is the exact four-argument slot
 * setter. This file joins those facts. It does not invent a second input map.
 *
 * Health/energy and the d-pad are deliberately absent here. The Xbox screen
 * puts packs on Black/White, but the PC table has no named pack row; it labels
 * the d-pad only "Change Hero", without exposing which PC action row each
 * direction drives. Guessing either would make the preset look complete while
 * changing gameplay. They remain the next RE boundary.
 */
#include "xbox_defaults.h"

#include "dinput_pad.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define EXE_PREFERRED       0x00400000u
#define CONTROLLER0_RVA     0x00668f40u
#define SET_BINDING_RVA     0x002297a0u
#define BINDINGS_OFFSET     0x18u
#define BINDING_ROWS        42u
#define PAD_SLOT            2u

/* DirectInput's Xbox-360 layout as exposed by dinput_pad.c. Axis pairs are
   positive then negative: LX 1/2, LY 3/4, Rx 7/8, Ry 9/10. POV is X+/X-/Y+/Y-
   at 0x11..0x14; buttons begin at 0x15 in A, B, X, Y order. */
static const XboxDefaultBinding DEFAULTS[] = {
    {  0, 0x04 }, /* Forward       LY- */
    {  1, 0x03 }, /* Backward      LY+ */
    {  2, 0x02 }, /* MoveLeft      LX- */
    {  3, 0x01 }, /* MoveRight     LX+ */
    {  4, 0x15 }, /* LowAttack     A / Punch */
    {  5, 0x16 }, /* HighAttack    B / Slam */
    {  6, 0x18 }, /* Jump          Y / Jump + Xtreme */
    {  7, 0x17 }, /* Guard         X / Use + Pickup + Boost */
    {  8, 0x06 }, /* Power         RT (combined DirectInput Z-) */
    {  9, 0x05 }, /* Ally          LT (combined DirectInput Z+) */
    { 16, 0x1e }, /* MapToggle     right-stick click / button 10 */
    { 17, 0x1c }, /* Pause         Start / button 8 */
    { 18, 0x1b }, /* Stats         Back / Team Information */
    { 19, 0x0a }, /* CameraUp      Ry- */
    { 20, 0x09 }, /* CameraDown    Ry+ */
    { 21, 0x08 }, /* CameraLeft    Rx- */
    { 22, 0x07 }, /* CameraRight   Rx+ */
};

static int g_installed;
static uint32_t g_kind;
static unsigned long g_applies, g_clears, g_custom, g_not_ready;

const XboxDefaultBinding *xbox_default_bindings(size_t *count)
{
    if (count) *count = sizeof DEFAULTS / sizeof DEFAULTS[0];
    return DEFAULTS;
}

static X86Module *exe_module(void)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == EXE_PREFERRED && m->base && *m->base)
            return m;
    return NULL;
}

static int first_pad(void)
{
    int i;
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (dinput_pad_name(i)) return i;
    return -1;
}

static int binding_object(X86Module *m, uint32_t *out)
{
    uint32_t controller;
    if (!x86_peek32(*m->base + CONTROLLER0_RVA, &controller) || !controller)
        return 0;
    *out = controller + BINDINGS_OFFSET;
    return x86_peek32(*out, &controller); /* constructor's row-count field */
}

static int read_slot(uint32_t object, uint32_t row, uint32_t *kind,
                     uint32_t *code)
{
    uint32_t slot = object + 4u + (row * 4u + PAD_SLOT) * 12u;
    return x86_peek32(slot + 4u, kind) && x86_peek32(slot + 8u, code);
}

static void set_slot(CPU *cpu, X86Module *m, uint32_t object, uint32_t row,
                     uint32_t kind, uint32_t code)
{
    CPU call = *cpu;
    call.ecx = object;
    call.esp -= 16u;
    WR32(call.esp + 0u, row);
    WR32(call.esp + 4u, PAD_SLOT);
    WR32(call.esp + 8u, kind);
    WR32(call.esp + 12u, code);
    x86_guest_call_args(&call, *m->base + SET_BINDING_RVA, 16u);
}

static int any_pad_binding(uint32_t object)
{
    uint32_t row, kind, code;
    for (row = 0; row < BINDING_ROWS; row++) {
        if (!read_slot(object, row, &kind, &code)) return -1;
        if (kind >= 3u && kind <= 0xcu) return 1;
    }
    return 0;
}

static void remove_installed(CPU *cpu, X86Module *m, uint32_t object)
{
    size_t i;
    uint32_t kind, code;
    for (i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++)
        if (read_slot(object, DEFAULTS[i].binding, &kind, &code) &&
            kind == g_kind && code == DEFAULTS[i].code)
            set_slot(cpu, m, object, DEFAULTS[i].binding, 0u, 0u);
    g_installed = 0;
    g_kind = 0;
    g_clears++;
}

void xbox_defaults_sync(CPU *cpu)
{
    X86Module *m;
    uint32_t object, kind;
    int pad, occupied;
    size_t i;

    if (!cpu || !(m = exe_module()) || !binding_object(m, &object)) {
        g_not_ready++;
        return;
    }
    pad = first_pad();
    kind = pad < 0 ? 0u : 3u + (uint32_t)pad;

    if (g_installed && kind != g_kind)
        remove_installed(cpu, m, object);
    if (!kind || g_installed) return;

    occupied = any_pad_binding(object);
    if (occupied != 0) {
        if (occupied > 0) g_custom++;
        else g_not_ready++;
        return;
    }
    for (i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++)
        set_slot(cpu, m, object, DEFAULTS[i].binding, kind, DEFAULTS[i].code);
    g_installed = 1;
    g_kind = kind;
    g_applies++;
    fprintf(stderr, "XBOX-DEFAULTS: installed %zu verified Xbox-release "
                    "bindings for "
                    "gamepad kind %u through FUN_006297a0; existing pad "
                    "bindings would have been preserved. D-pad and Health/"
                    "Energy remain unmapped pending their separate PC action "
                    "boundaries.\n",
            sizeof DEFAULTS / sizeof DEFAULTS[0], kind);
}

void __real_fn_XMen2_0061b030(CPU *cpu);

void __wrap_fn_XMen2_0061b030(CPU *cpu)
{
    __real_fn_XMen2_0061b030(cpu);
    xbox_defaults_sync(cpu);
}

void xbox_defaults_report(void)
{
    printf("  Xbox defaults: %lu install(s), %lu removal(s), %lu custom-map "
           "deferral(s), %lu not-ready probe(s); %s\n",
           g_applies, g_clears, g_custom, g_not_ready,
           g_installed ? "port preset active" : "port preset inactive");
}
