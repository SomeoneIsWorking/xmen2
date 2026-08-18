/*
 * XMen2.exe's controller binding table -- the storage the whole input feature
 * set sits on.
 *
 * This file owns the ABI and nothing else: where the table is, how a row and
 * slot are addressed, what each row is called, and which row an action id
 * resolves to. The Xbox default layout, the on-screen prompts and the live
 * probe all read it from here rather than each carrying its own copy of the
 * arithmetic.
 */
#include "input_bindings.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define EXE_PREFERRED    0x00400000u
#define CONTROLLER0_RVA  0x00668f40u  /* 0x00a68f40, controller[player]      */
#define BINDINGS_OFFSET  0x18u        /* the binding object inside it        */
#define SET_BINDING_RVA  0x002297a0u  /* FUN_006297a0(row, slot, kind, code) */
#define ROW_OF_ACTION_RVA 0x00219c40u /* FUN_00619c40(action) -> row         */

/*
 * The 42 row names, read out of FUN_0061b030's own name array (the strings it
 * formats into `Controls\Player%d\%s1`). They are the game's spelling, typo
 * included -- "SreenGrab" is what the binary says, and correcting it here
 * would silently stop matching the registry key the game writes.
 *
 * tools/binding_rows.py re-extracts this list from the shipped exe and
 * tests/test_input_bindings.c diffs the two, so this table cannot drift away
 * from the measurement it came from without a test failing.
 */
static const char *const ROW_NAMES[INPUT_BINDING_ROWS] = {
    "Forward", "Backward", "MoveLeft", "MoveRight",
    "LowAttack", "HighAttack", "Jump", "Guard",
    "Power", "Ally", "TargetLock", "Solo",
    "NextHero", "PreviousHero", "DecreaseHeroAggr", "IncreaseHeroAggr",
    "MapToggle", "Pause", "Stats",
    "CameraUp", "CameraDown", "CameraLeft", "CameraRight",
    "SreenGrab", "Talk", "Walk", "SwtHero", "AttackObject", "RotateCamera",
    "BindPower", "UseQuickPower",
    "QuickPower01", "QuickPower02", "QuickPower03", "QuickPower04",
    "QuickPower05", "QuickPower06", "QuickPower07", "QuickPower08",
    "QuickPower09", "QuickPower10", "QuickPower11"
};

const char *input_binding_row_name(uint32_t row)
{
    return row < INPUT_BINDING_ROWS ? ROW_NAMES[row] : NULL;
}

static uint32_t exe_base(void)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == EXE_PREFERRED && m->base && *m->base)
            return *m->base;
    return 0;
}

uint32_t input_bindings_object_at(uint32_t index, char *why, int whyn)
{
    uint32_t base = exe_base(), slot, controller = 0, object;

    if (why && whyn > 0) snprintf(why, (size_t)whyn, "(no reason recorded)");
    if (index >= INPUT_CONTROLLERS) {
        if (why) snprintf(why, (size_t)whyn,
                          "controller %u does not exist: the game keeps %u",
                          index, INPUT_CONTROLLERS);
        return 0;
    }
    if (!base) {
        if (why) snprintf(why, (size_t)whyn,
                          "XMen2.exe is not mapped, so the binding table has "
                          "no address yet");
        return 0;
    }
    slot = base + CONTROLLER0_RVA + index * 4u;
    if (!x86_peek32(slot, &controller) || !controller) {
        if (why) snprintf(why, (size_t)whyn,
                          "the game has not constructed controller %u yet "
                          "([0x%08x] is 0) -- too early, not unbound",
                          index, slot);
        return 0;
    }
    object = controller + BINDINGS_OFFSET;
    if (!x86_peek32(object, &controller)) {
        if (why) snprintf(why, (size_t)whyn,
                          "controller %u's binding object at 0x%08x is not "
                          "readable guest memory", index, object);
        return 0;
    }
    return object;
}

uint32_t input_bindings_object(char *why, int whyn)
{
    return input_bindings_object_at(0u, why, whyn);
}

/*
 * Row `row`, slot `slot`. Read straight off the setter's own arithmetic
 * (FUN_006297a0 at 0x006297bf): `EAX = slot + row*4`, `EAX *= 3`, and the
 * element is `this + EAX*4` -- so the array starts AT the object, with no
 * leading field, and each element is three dwords whose +4 is the device kind
 * and +8 the code. FUN_006294b0 reads the same two offsets back.
 *
 * Element 0's first dword doubles as the object's row count ([ECX] in both
 * functions); the game overlaps them deliberately and nothing reads element
 * 0's +0 as a binding.
 */
static uint32_t slot_addr(uint32_t object, uint32_t row, uint32_t slot)
{
    return object + (row * INPUT_BINDING_SLOTS + slot) * 12u;
}

int input_bindings_read(uint32_t object, uint32_t row, uint32_t slot,
                        uint32_t *kind, uint32_t *code)
{
    uint32_t a;
    if (!object || row >= INPUT_BINDING_ROWS || slot >= INPUT_BINDING_SLOTS)
        return 0;
    a = slot_addr(object, row, slot);
    return x86_peek32(a + 4u, kind) && x86_peek32(a + 8u, code);
}

void input_bindings_write(CPU *cpu, uint32_t object, uint32_t row,
                          uint32_t slot, uint32_t kind, uint32_t code)
{
    CPU call;
    uint32_t base = exe_base();

    if (!cpu || !base || !object) return;
    call = *cpu;
    call.ecx = object;
    call.esp -= 16u;
    WR32(call.esp +  0u, row);
    WR32(call.esp +  4u, slot);
    WR32(call.esp +  8u, kind);
    WR32(call.esp + 12u, code);
    x86_guest_call_args(&call, base + SET_BINDING_RVA, 16u);
}

unsigned input_bindings_write_player(CPU *cpu, uint32_t player, uint32_t row,
                                     uint32_t slot, uint32_t kind,
                                     uint32_t code)
{
    static const uint32_t BANK[INPUT_BINDING_SETS] = {
        INPUT_SET_MASTER, INPUT_SET_WORKING, INPUT_SET_MENU
    };
    unsigned i, done = 0;
    char why[192];

    if (player >= INPUT_PLAYERS) return 0;
    for (i = 0; i < INPUT_BINDING_SETS; i++) {
        uint32_t object = input_bindings_object_at(BANK[i] + player, why,
                                                   (int)sizeof why);
        if (!object) continue;
        input_bindings_write(cpu, object, row, slot, kind, code);
        done++;
    }
    return done;
}

int input_binding_row_of_action(CPU *cpu, uint32_t action)
{
    CPU call;
    uint32_t base = exe_base();

    if (!cpu || !base || action >= INPUT_ACTION_MAX) return -1;
    call = *cpu;
    /* FUN_00619c40 ends in a plain RET: it is cdecl, so it pops nothing and
       the caller cleans. The CPU copy is discarded, which IS that cleanup. */
    call.esp -= 4u;
    WR32(call.esp, action);
    x86_guest_call_args(&call, base + ROW_OF_ACTION_RVA, 0u);
    return (int32_t)call.eax;
}
