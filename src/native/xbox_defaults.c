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
 * The Xbox action constructor at 0x00162240 binds d-pad Up/Down/Right/Left to
 * NEXT/PREV/INC_AGGR/DEC_AGGR. FUN_00619c40 maps those four action IDs to the
 * correspondingly named PC rows. Black/White remain outside this table: the
 * Xbox action constructor does not register either against a common action,
 * and the PC binding object has no Health/Energy row. Their direct gameplay
 * path is a separate engine boundary, not a reason to invent aliases here.
 *
 * WHERE the preset is written is as load-bearing as what it contains, and
 * getting it wrong is silent -- the bindings sit in the table, the options UI
 * shows them, and the running game never reads one. Two facts settle it, both
 * out of FUN_0061b030 and both re-checkable live with `x2ctl.py input`:
 *
 *   * SLOT 1, not slot 2. The game rewrites slots 2 and 3 of the working and
 *     menu sets with its own hardcoded keys AFTER copying the master (row 4
 *     slot 2 is Return -- the [ENTER] a dialog prompt shows). Slot 1's default
 *     is 0 on every row, and it is the second binding the registry persists,
 *     so it is the alternate the game itself leaves for a second device.
 *
 *   * ALL THREE SETS, not the master alone. The master is copied into the
 *     working and menu sets once, inside FUN_0061b030. This runs after that,
 *     so it publishes the write itself -- see input_bindings_write_player.
 */
#include "xbox_defaults.h"

#include "dinput_pad.h"
#include "input_bindings.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define BINDING_ROWS        INPUT_BINDING_ROWS
#define PAD_SLOT            INPUT_BINDING_ALT_SLOT
#define PAD_PLAYER          0u      /* the pad drives player 1 */

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
    { 12, 0x14 }, /* NextHero      d-pad up */
    { 13, 0x13 }, /* PreviousHero  d-pad down */
    { 14, 0x12 }, /* DecreaseAggr  d-pad left */
    { 15, 0x11 }, /* IncreaseAggr  d-pad right */
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
static unsigned long g_applies, g_explicit, g_clears, g_custom, g_not_ready;

const XboxDefaultBinding *xbox_default_bindings(size_t *count)
{
    if (count) *count = sizeof DEFAULTS / sizeof DEFAULTS[0];
    return DEFAULTS;
}

static int first_pad(void)
{
    int i;
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (dinput_pad_name(i)) return i;
    return -1;
}

static int read_slot(uint32_t object, uint32_t row, uint32_t *kind,
                     uint32_t *code)
{
    return input_bindings_read(object, row, PAD_SLOT, kind, code);
}

/* Every write goes to the master AND to the copies the game evaluates. */
static void set_slot(CPU *cpu, uint32_t row, uint32_t kind, uint32_t code)
{
    input_bindings_write_player(cpu, PAD_PLAYER, row, PAD_SLOT, kind, code);
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

static void clear_pad_slot(CPU *cpu, uint32_t object)
{
    uint32_t row, kind, code;
    for (row = 0; row < BINDING_ROWS; row++)
        if (read_slot(object, row, &kind, &code) && (kind || code))
            set_slot(cpu, row, 0u, 0u);
}

static void install_defaults(CPU *cpu, uint32_t kind)
{
    size_t i;
    for (i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++)
        set_slot(cpu, DEFAULTS[i].binding, kind, DEFAULTS[i].code);
}

static void remove_installed(CPU *cpu, uint32_t object)
{
    size_t i;
    uint32_t kind, code;
    for (i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++)
        if (read_slot(object, DEFAULTS[i].binding, &kind, &code) &&
            kind == g_kind && code == DEFAULTS[i].code)
            set_slot(cpu, DEFAULTS[i].binding, 0u, 0u);
    g_installed = 0;
    g_kind = 0;
    g_clears++;
}

void xbox_defaults_sync(CPU *cpu)
{
    uint32_t object, kind;
    int pad, occupied;
    char why[192];

    if (!cpu || !(object = input_bindings_object(why, (int)sizeof why))) {
        g_not_ready++;
        return;
    }
    pad = first_pad();
    kind = pad < 0 ? 0u : 3u + (uint32_t)pad;

    if (g_installed && kind != g_kind)
        remove_installed(cpu, object);
    if (!kind || g_installed) return;

    occupied = any_pad_binding(object);
    if (occupied != 0) {
        if (occupied > 0) g_custom++;
        else g_not_ready++;
        return;
    }
    install_defaults(cpu, kind);
    g_installed = 1;
    g_kind = kind;
    g_applies++;
    fprintf(stderr, "XBOX-DEFAULTS: installed %zu verified Xbox-release "
                    "bindings for "
                    "gamepad kind %u through FUN_006297a0; existing pad "
                    "bindings would have been preserved. Black/White Health/"
                    "Energy remain a separate direct-action boundary.\n",
            sizeof DEFAULTS / sizeof DEFAULTS[0], kind);
}

int xbox_defaults_apply(CPU *cpu)
{
    uint32_t object, kind;
    int pad;
    char why[192];

    if (!cpu || !(object = input_bindings_object(why, (int)sizeof why))) {
        g_not_ready++;
        return 0;
    }
    pad = first_pad();
    if (pad < 0) return 0;
    kind = 3u + (uint32_t)pad;

    clear_pad_slot(cpu, object);
    install_defaults(cpu, kind);

    /* The explicit command converts an automatic installation into persisted
       user-selected state. Disconnect cleanup must not erase that choice. */
    g_installed = 0;
    g_kind = 0;
    g_explicit++;
    return 1;
}

void fn_XMen2_0061b030(CPU *cpu);

void x2_override_0061b030(CPU *cpu)
{
    fn_XMen2_0061b030(cpu);
    xbox_defaults_sync(cpu);
}

/* Register the Xbox-defaults binding override. */
__attribute__((constructor))
static void x2_xbox_defaults_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x0061b030, x2_override_0061b030);
}

void xbox_defaults_report(void)
{
    printf("  Xbox defaults: %lu automatic, %lu explicit, %lu removal(s), "
           "%lu custom-map "
           "deferral(s), %lu not-ready probe(s); %s\n",
           g_applies, g_explicit, g_clears, g_custom, g_not_ready,
           g_installed ? "port preset active" : "port preset inactive");
}
