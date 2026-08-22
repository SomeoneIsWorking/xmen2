#include "player_input.h"

#include "dinput_pad.h"
#include "input_bindings.h"
#include "player_participation.h"
#include "player_participation_policy.h"
#include "settings_store.h"
#include "transient_controller_assignment.h"
#include "xbox_defaults.h"
#include "x86rt.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t kind;
    uint32_t code;
} Binding;

_Static_assert(X2_SETTINGS_ROWS == INPUT_BINDING_ROWS,
               "settings profiles must cover every shipping binding row");

static Binding g_keyboard_base[INPUT_BINDING_ROWS];
static X2Settings g_last;
static int g_have_base;
static int g_have_last;
static int g_last_pad[INPUT_PLAYERS] = { -2, -2, -2, -2 };
static int g_last_keyboard[INPUT_PLAYERS] = { -2, -2, -2, -2 };
static int g_last_source_gamepad[INPUT_PLAYERS];
static unsigned char g_last_keyboard_state[256];
static X2PlayerParticipationPolicy g_participation;
static int g_have_participation;

#define PAUSE_ROW 17u
#define DI_JOYSTATE_BUTTONS 48u

static int capture_keyboard_base(void)
{
    unsigned row;
    char why[192];
    uint32_t object = input_bindings_object_at(INPUT_SET_MASTER,
                                               why, (int)sizeof why);
    if (!object) return 0;
    for (row = 0; row < INPUT_BINDING_ROWS; row++)
        if (!input_bindings_read(object, row, 0,
                                 &g_keyboard_base[row].kind,
                                 &g_keyboard_base[row].code))
            return 0;
    g_have_base = 1;
    return 1;
}

static void resolve_pads(const X2Settings *settings, int out[INPUT_PLAYERS])
{
    int claimed[DINPUT_PAD_MAX] = {0};
    unsigned player;
    int pad;

    for (player = 0; player < INPUT_PLAYERS; player++) out[player] = -1;
    /* A process-lifetime assignment names one exact live instance. It wins
       over persisted reservations even while disconnected, so slot reuse or
       a different stored controller cannot make the player roam. */
    for (player = 0; player < INPUT_PLAYERS; player++) {
        if (x2_transient_controller_has_assignment(player)) {
            pad = x2_transient_controller_resolve(player);
            if (pad >= 0 && !claimed[pad]) {
                out[player] = pad;
                claimed[pad] = 1;
            }
        }
    }
    for (player = 0; player < INPUT_PLAYERS; player++) {
        const char *id = x2_settings_player_controller(settings, player);
        if (x2_transient_controller_has_assignment(player)) continue;
        if (!id) continue;
        pad = dinput_pad_for_persistent_id(id);
        if (pad >= 0 && !claimed[pad]) {
            out[player] = pad;
            claimed[pad] = 1;
        }
    }
}

static uint32_t default_gamepad_code(unsigned row)
{
    const XboxDefaultBinding *defaults;
    size_t count, i;
    defaults = xbox_default_bindings(&count);
    for (i = 0; i < count; i++)
        if (defaults[i].binding == row) return defaults[i].code;
    return 0;
}

static int keyboard_code(const X2Settings *settings, int profile_index,
                         unsigned row, uint32_t *code)
{
    const X2KeyboardProfile *profile;
    uint32_t kind;

    if (!settings || profile_index < 0 || row >= INPUT_BINDING_ROWS) return 0;
    profile = &settings->keyboard_profile[profile_index];
    kind = g_keyboard_base[row].kind;
    *code = g_keyboard_base[row].code;
    if (profile->keyboard_set[row]) {
        kind = profile->keyboard[row] ? 1u : 0u;
        *code = profile->keyboard[row];
    }
    return kind == 1u;
}

static uint8_t eligibility_mask(const X2Settings *settings,
                                const int keyboard[INPUT_PLAYERS])
{
    uint8_t eligible = 0;
    unsigned player;
    for (player = 0; player < INPUT_PLAYERS; player++)
        if (keyboard[player] >= 0 ||
            x2_transient_controller_has_assignment(player) ||
            x2_settings_player_controller(settings, player))
            eligible |= (uint8_t)(1u << player);
    return eligible;
}

static void sync_participation(CPU *cpu, const X2Settings *settings,
                               const int keyboard[INPUT_PLAYERS])
{
    X2PlayerParticipationTransition transition;
    uint8_t eligible;
    if (!g_have_participation) {
        x2_player_participation_policy_init(&g_participation);
        g_have_participation = 1;
    }
    eligible = eligibility_mask(settings, keyboard);
    x2_player_participation_policy_configure(&g_participation, eligible);
    transition = x2_player_participation_policy_consume(&g_participation);
    x2_player_participation_apply(cpu, transition.join, transition.leave);
    x2_player_participation_enforce_eligibility(cpu, eligible);
}

static void publish_player(CPU *cpu, const X2Settings *settings,
                           unsigned player, int keyboard_profile, int pad)
{
    const X2KeyboardProfile *profile = keyboard_profile >= 0
        ? &settings->keyboard_profile[keyboard_profile] : NULL;
    unsigned row;
    uint32_t pad_kind = pad < 0 ? 0u : 3u + (uint32_t)pad;

    for (row = 0; row < INPUT_BINDING_ROWS; row++) {
        uint32_t keyboard_kind = 0, keyboard_code = 0, pad_code = 0;
        if (profile) {
            keyboard_kind = g_keyboard_base[row].kind;
            keyboard_code = g_keyboard_base[row].code;
            if (profile->keyboard_set[row]) {
                keyboard_kind = profile->keyboard[row] ? 1u : 0u;
                keyboard_code = profile->keyboard[row];
            }
        }
        if (pad_kind) {
            pad_code = default_gamepad_code(row);
        }
        input_bindings_write_player(cpu, player, row, 0, keyboard_kind,
                                    keyboard_code);
        input_bindings_write_player(cpu, player, row,
                                    INPUT_BINDING_ALT_SLOT,
                                    pad_code ? pad_kind : 0u, pad_code);
    }
}

void x2_player_input_sync(CPU *cpu)
{
    X2Settings *settings;
    int pad[INPUT_PLAYERS];
    int keyboard[INPUT_PLAYERS];
    unsigned player;
    int changed;

    if (!cpu) return;
    dinput_pad_refresh();
    if (!g_have_base && !capture_keyboard_base()) return;
    settings = x2_settings_store();
    resolve_pads(settings, pad);
    for (player = 0; player < INPUT_PLAYERS; player++) {
        keyboard[player] = x2_settings_player_keyboard(settings, player);
        if (player > 0u &&
            x2_transient_controller_has_assignment(player))
            keyboard[player] = -1;
    }
    changed = !g_have_last || memcmp(&g_last, settings, sizeof g_last) != 0 ||
              memcmp(g_last_pad, pad, sizeof pad) != 0 ||
              memcmp(g_last_keyboard, keyboard, sizeof keyboard) != 0;
    if (!changed) {
        sync_participation(cpu, settings, keyboard);
        return;
    }
    for (player = 0; player < INPUT_PLAYERS; player++) {
        if (g_have_last &&
            (g_last_pad[player] != pad[player] ||
             g_last_keyboard[player] != keyboard[player]))
            x2_player_participation_policy_note_start(
                &g_participation, player, 0);
        publish_player(cpu, settings, player, keyboard[player], pad[player]);
        if (pad[player] >= 0 && keyboard[player] < 0)
            g_last_source_gamepad[player] = 1;
        else if (keyboard[player] >= 0 && pad[player] < 0)
            g_last_source_gamepad[player] = 0;
    }
    g_last = *settings;
    memcpy(g_last_pad, pad, sizeof pad);
    memcpy(g_last_keyboard, keyboard, sizeof keyboard);
    g_have_last = 1;
    sync_participation(cpu, settings, keyboard);
    fprintf(stderr, "PLAYER-INPUT: published resolved ownership for four "
                    "players (pads %d,%d,%d,%d); each physical pad is claimed "
                    "by at most one player.\n", pad[0], pad[1], pad[2], pad[3]);
}

int x2_player_input_uses_gamepad(unsigned player)
{
    return g_have_last && player < INPUT_PLAYERS && g_last_pad[player] >= 0 &&
           (g_last_keyboard[player] < 0 || g_last_source_gamepad[player]);
}

void x2_player_input_note_keyboard_state(const unsigned char *state,
                                         unsigned bytes)
{
    unsigned player;
    if (!state || !g_have_last) return;
    for (player = 0; player < INPUT_PLAYERS; player++) {
        int profile_index = g_last_keyboard[player];
        unsigned row;
        if (profile_index < 0) continue;
        for (row = 0; row < INPUT_BINDING_ROWS; row++) {
            const X2KeyboardProfile *profile =
                &g_last.keyboard_profile[profile_index];
            uint32_t kind = g_keyboard_base[row].kind;
            uint32_t code = g_keyboard_base[row].code;
            if (profile->keyboard_set[row]) {
                kind = profile->keyboard[row] ? 1u : 0u;
                code = profile->keyboard[row];
            }
            if (kind == 1u && code < bytes && (state[code] & 0x80u)) {
                g_last_source_gamepad[player] = 0;
                break;
            }
        }
    }

    if (g_have_participation) {
        for (player = 0; player < INPUT_PLAYERS; player++) {
            uint32_t code;
            unsigned other;
            int ambiguous = 0;
            if (!keyboard_code(&g_last, g_last_keyboard[player], PAUSE_ROW,
                               &code) || code >= bytes || code >= 256u)
                continue;
            if (!(state[code] & 0x80u)) {
                x2_player_participation_policy_note_start(
                    &g_participation, player, 0);
                continue;
            }
            if (g_last_keyboard_state[code] & 0x80u) continue;
            for (other = 0; other < INPUT_PLAYERS; other++) {
                uint32_t other_code;
                if (other == player ||
                    !keyboard_code(&g_last, g_last_keyboard[other], PAUSE_ROW,
                                   &other_code))
                    continue;
                if (other_code == code) {
                    ambiguous = 1;
                    break;
                }
            }
            if (!ambiguous)
                x2_player_participation_policy_note_start(
                    &g_participation, player, 1);
        }
    }
    memcpy(g_last_keyboard_state, state,
           bytes < sizeof g_last_keyboard_state
               ? bytes : sizeof g_last_keyboard_state);
}

void x2_player_input_note_gamepad_activity(int pad)
{
    unsigned player;
    for (player = 0; player < INPUT_PLAYERS; player++)
        if (g_have_last && g_last_pad[player] == pad)
            g_last_source_gamepad[player] = 1;
}

void x2_player_input_note_gamepad_state(int pad, const unsigned char *state,
                                        unsigned bytes)
{
    uint32_t code = default_gamepad_code(PAUSE_ROW);
    unsigned button;
    unsigned player;
    int down;

    if (!state || pad < 0 || pad >= DINPUT_PAD_MAX || code < 0x15u) return;
    button = code - 0x15u;
    if (DI_JOYSTATE_BUTTONS + button >= bytes) return;
    down = (state[DI_JOYSTATE_BUTTONS + button] & 0x80u) != 0;
    for (player = 0; player < INPUT_PLAYERS; player++)
        if (g_have_last && g_last_pad[player] == pad)
            x2_player_participation_policy_note_start(
                &g_participation, player, down);
}

int x2_player_input_pad_is_active_source(int pad)
{
    unsigned player;
    for (player = 0; player < INPUT_PLAYERS; player++)
        if (g_have_last && g_last_pad[player] == pad)
            return g_last_keyboard[player] < 0 ||
                   g_last_source_gamepad[player];
    return 0;
}

int x2_player_input_resolved_pad(unsigned player)
{
    return g_have_last && player < INPUT_PLAYERS ? g_last_pad[player] : -1;
}
