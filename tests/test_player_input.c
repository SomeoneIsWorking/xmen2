#include "player_input.h"

#include "dinput_pad.h"
#include "input_binding_sets.h"
#include "input_bindings.h"
#include "settings_store.h"
#include "x86rt.h"
#include "xbox_defaults.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t kind, code; } Slot;
static Slot slots[INPUT_CONTROLLERS][INPUT_BINDING_ROWS][INPUT_BINDING_SLOTS];
static X2Settings settings;
static const char *pads[DINPUT_PAD_MAX];
static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

X2Settings *x2_settings_store(void) { return &settings; }
void dinput_pad_refresh(void) {}
const char *dinput_pad_name(int pad) { return pads[pad]; }
int dinput_pad_for_persistent_id(const char *id)
{
    int i;
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (pads[i] && strcmp(pads[i], id) == 0) return i;
    return -1;
}
uint32_t input_bindings_object_at(uint32_t index, char *why, int whyn)
{
    (void)why; (void)whyn;
    return index + 1u;
}
int input_bindings_read(uint32_t object, uint32_t row, uint32_t slot,
                        uint32_t *kind, uint32_t *code)
{
    Slot *s = &slots[object - 1u][row][slot];
    *kind = s->kind; *code = s->code;
    return 1;
}
typedef struct {
    uint32_t row, slot, kind, code;
} TestBindingWrite;

static void write_test_set(uint32_t controller, void *context)
{
    TestBindingWrite *write = context;
    slots[controller][write->row][write->slot] =
        (Slot){write->kind, write->code};
}

unsigned input_bindings_write_player(CPU *cpu, uint32_t player, uint32_t row,
                                     uint32_t slot, uint32_t kind, uint32_t code)
{
    TestBindingWrite write = {row, slot, kind, code};
    (void)cpu;
    return input_binding_sets_for_player(player, write_test_set, &write);
}
int main(void)
{
    CPU cpu = {0};
    unsigned char keyboard_state[256] = {0};
    unsigned player, row;
    for (player = 0; player < INPUT_PLAYERS; player++)
        for (row = 0; row < INPUT_BINDING_ROWS; row++)
            slots[player][row][0] = (Slot){1, 20u + row};
    for (player = 0; player < INPUT_PLAYERS; player++)
        slots[player][17][0] = (Slot){1, 0x01};
    x2_settings_defaults(&settings);
    pads[0] = "pad-a";
    pads[1] = "pad-b";
    CHECK(x2_settings_assign_controller(&settings, "pad-b", 1));
    CHECK(x2_settings_assign_controller(&settings, "pad-a", 2));
    CHECK(x2_settings_assign_keyboard(&settings, 2, 3));
    settings.keyboard_profile[2].keyboard_set[4] = 1;
    settings.keyboard_profile[2].keyboard[4] = 77;
    x2_player_input_sync(&cpu);

    /* Each persistent controller identity resolves only for its owner. */
    CHECK(slots[0][4][INPUT_BINDING_ALT_SLOT].kind == 0);
    CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 4);
    CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 3);
    CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].code == 0x15);
    CHECK(slots[2][8][INPUT_BINDING_ALT_SLOT].code == 0x06);
    CHECK(slots[0][4][0].kind == 1);
    CHECK(slots[1][4][0].kind == 0);
    CHECK(slots[3][4][0].kind == 1);
    CHECK(slots[3][4][0].code == 77);
    CHECK(!x2_player_input_uses_gamepad(0));
    CHECK(x2_player_input_resolved_pad(1) == 1);
    CHECK(x2_player_input_resolved_pad(2) == 0);

    /* The exact production publisher must carry each retail Pause input to
       every bank the game evaluates: master, working and menu. */
    for (row = 0; row < 3; row++) {
        static const unsigned bank[] = { 0, 4, 12 };
        CHECK(slots[bank[row] + 0][17][0].kind == 1);
        CHECK(slots[bank[row] + 0][17][0].code == 0x01);
        CHECK(slots[bank[row] + 1][17][INPUT_BINDING_ALT_SLOT].kind == 4);
        CHECK(slots[bank[row] + 1][17][INPUT_BINDING_ALT_SLOT].code == 0x1c);
        CHECK(slots[bank[row] + 2][17][INPUT_BINDING_ALT_SLOT].kind == 3);
        CHECK(slots[bank[row] + 2][17][INPUT_BINDING_ALT_SLOT].code == 0x1c);
    }

    /* Moving a keyboard row evicts the prior keyboard row for that player. */
    CHECK(x2_settings_assign_keyboard(&settings, 2, 0));
    x2_player_input_sync(&cpu);
    CHECK(slots[0][4][0].code == 77);

    /* Disconnecting an assigned pad leaves its association reserved. */
    pads[1] = NULL;
    x2_player_input_sync(&cpu);
    CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 0);
    CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 3);
    CHECK(slots[0][4][0].kind == 1);
    CHECK(!x2_player_input_uses_gamepad(1));

    pads[1] = "pad-b";
    x2_player_input_sync(&cpu);
    CHECK(x2_player_input_resolved_pad(1) == 1);

    /* Keyboard plus a specific controller is implicit hotswap. Both binding
       sources are published simultaneously; prompt mode follows activity. */
    CHECK(x2_settings_assign_controller(&settings, "hot-pad", 0));
    pads[0] = "hot-pad";
    pads[1] = "pad-b";
    x2_player_input_sync(&cpu);
    CHECK(x2_player_input_resolved_pad(0) == 0);
    CHECK(slots[0][4][0].kind == 1);
    CHECK(slots[0][4][INPUT_BINDING_ALT_SLOT].kind == 3);
    CHECK(!x2_player_input_uses_gamepad(0));
    x2_player_input_note_gamepad_activity(0);
    CHECK(x2_player_input_uses_gamepad(0));
    keyboard_state[77] = 0x80;
    x2_player_input_note_keyboard_state(keyboard_state,
                                        sizeof keyboard_state);
    CHECK(!x2_player_input_uses_gamepad(0));

    /* Disconnect waits for the same persistent identity. A different pad in
       the reused slot is not adopted; the exact identity resumes hotswap. */
    pads[0] = NULL;
    x2_player_input_sync(&cpu);
    CHECK(!x2_player_input_uses_gamepad(0));
    pads[0] = "other-pad";
    x2_player_input_sync(&cpu);
    CHECK(x2_player_input_resolved_pad(0) == -1);
    pads[0] = "hot-pad";
    x2_player_input_sync(&cpu);
    CHECK(!x2_player_input_uses_gamepad(0));
    CHECK(x2_player_input_resolved_pad(0) == 0);

    printf("test_player_input: %d checks passed\n", checks);
    return 0;
}
