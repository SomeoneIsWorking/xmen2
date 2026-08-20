#include "player_input.h"

#include "dinput_pad.h"
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
unsigned input_bindings_write_player(CPU *cpu, uint32_t player, uint32_t row,
                                     uint32_t slot, uint32_t kind, uint32_t code)
{
    static const unsigned bank[] = { 0, 4, 12 };
    unsigned i;
    (void)cpu;
    for (i = 0; i < 3; i++) slots[bank[i] + player][row][slot] = (Slot){kind, code};
    return 3;
}
const XboxDefaultBinding *xbox_default_bindings(size_t *count)
{
    static const XboxDefaultBinding defaults[] = { {4, 0x15}, {8, 0x06} };
    *count = sizeof defaults / sizeof defaults[0];
    return defaults;
}

int main(void)
{
    CPU cpu = {0};
    unsigned player, row;
    for (player = 0; player < INPUT_PLAYERS; player++)
        for (row = 0; row < INPUT_BINDING_ROWS; row++)
            slots[player][row][0] = (Slot){1, 20u + row};
    x2_settings_defaults(&settings);
    pads[0] = "pad-a";
    pads[1] = "pad-b";
    settings.player[1].type = X2_PLAYER_GAMEPAD;
    snprintf(settings.player[1].id, sizeof settings.player[1].id, "pad-b");
    settings.player[2].type = X2_PLAYER_GAMEPAD;
    snprintf(settings.player[2].id, sizeof settings.player[2].id, "pad-a");
    settings.player[3].type = X2_PLAYER_KEYBOARD;
    settings.player[3].keyboard_profile = 2;
    settings.keyboard_profile[2].keyboard_set[4] = 1;
    settings.keyboard_profile[2].keyboard[4] = 77;
    x2_player_input_sync(&cpu);

    /* Explicit identities reserve both pads before Player 1's Auto policy. */
    CHECK(slots[0][4][INPUT_BINDING_ALT_SLOT].kind == 0);
    CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 4);
    CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 3);
    CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].code == 0x15);
    CHECK(slots[2][8][INPUT_BINDING_ALT_SLOT].code == 0x06);
    CHECK(slots[0][4][0].kind == 1); /* Auto falls back to keyboard. */
    CHECK(slots[1][4][0].kind == 0);
    CHECK(slots[3][4][0].kind == 1);
    CHECK(slots[3][4][0].code == 77);

    /* A keyboard profile is reusable and is independent of player number. */
    settings.player[0].type = X2_PLAYER_KEYBOARD;
    settings.player[0].keyboard_profile = 2;
    x2_player_input_sync(&cpu);
    CHECK(slots[0][4][0].code == 77);

    /* Disconnecting the explicitly assigned pad does not let another player
       steal pad-a; the absent association becomes inactive. */
    pads[1] = NULL;
    x2_player_input_sync(&cpu);
    CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 0);
    CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 3);
    CHECK(slots[0][4][0].kind == 1);

    printf("test_player_input: %d checks passed\n", checks);
    return 0;
}
