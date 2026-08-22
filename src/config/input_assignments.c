#include "input_assignments.h"

#include <stdio.h>
#include <string.h>

int x2_settings_input_owner_valid(int player)
{
    return player == X2_SETTINGS_UNASSIGNED ||
           (player >= 0 && player < (int)X2_SETTINGS_PLAYERS);
}

static int player_has_keyboard(const X2Settings *settings, unsigned player)
{
    unsigned i;
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
        if (settings->keyboard_player[i] == (int)player) return 1;
    return 0;
}

static int player_has_controller(const X2Settings *settings, unsigned player)
{
    unsigned i;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if (settings->controller[i].player == (int)player) return 1;
    return 0;
}

int x2_settings_input_assignments_valid(const X2Settings *settings)
{
    unsigned player;
    if (!settings || (!player_has_keyboard(settings, 0u) &&
                      !player_has_controller(settings, 0u)))
        return 0;
    for (player = 1; player < X2_SETTINGS_PLAYERS; player++)
        if (player_has_keyboard(settings, player) &&
            player_has_controller(settings, player))
            return 0;
    return 1;
}

static void clear_player_controllers(X2Settings *settings, unsigned player)
{
    unsigned i;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if (settings->controller[i].player == (int)player) {
            memset(&settings->controller[i], 0, sizeof settings->controller[i]);
            settings->controller[i].player = X2_SETTINGS_UNASSIGNED;
        }
}

static void clear_player_keyboards(X2Settings *settings, unsigned player)
{
    unsigned i;
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
        if (settings->keyboard_player[i] == (int)player)
            settings->keyboard_player[i] = X2_SETTINGS_UNASSIGNED;
}

int x2_settings_assign_keyboard(X2Settings *settings, unsigned profile,
                                int player)
{
    X2Settings changed;
    unsigned i;
    if (!settings || profile >= X2_SETTINGS_KEYBOARD_PROFILES ||
        !x2_settings_input_owner_valid(player)) return 0;
    changed = *settings;
    if (player >= 0)
        for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
            if (changed.keyboard_player[i] == player)
                changed.keyboard_player[i] = X2_SETTINGS_UNASSIGNED;
    changed.keyboard_player[profile] = (int8_t)player;
    if (player > 0) clear_player_controllers(&changed, (unsigned)player);
    if (!x2_settings_input_assignments_valid(&changed)) return 0;
    *settings = changed;
    return 1;
}

static int controller_slot(const X2Settings *settings, const char *id)
{
    unsigned i;
    if (!id || !id[0]) return -1;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if (strcmp(settings->controller[i].id, id) == 0) return (int)i;
    return -1;
}

int x2_settings_assign_controller(X2Settings *settings, const char *id,
                                  int player)
{
    X2Settings changed;
    int slot;
    unsigned i;
    if (!settings || !id || !id[0] || strlen(id) >= X2_SETTINGS_DEVICE_ID ||
        !x2_settings_input_owner_valid(player)) return 0;
    changed = *settings;
    slot = controller_slot(&changed, id);
    if (player == X2_SETTINGS_UNASSIGNED) {
        if (slot >= 0) {
            memset(&changed.controller[slot], 0,
                   sizeof changed.controller[slot]);
            changed.controller[slot].player = X2_SETTINGS_UNASSIGNED;
        }
        if (!x2_settings_input_assignments_valid(&changed)) return 0;
        *settings = changed;
        return 1;
    }
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if ((int)i != slot && changed.controller[i].player == player) {
            memset(&changed.controller[i], 0, sizeof changed.controller[i]);
            changed.controller[i].player = X2_SETTINGS_UNASSIGNED;
        }
    if (slot < 0)
        for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
            if (!changed.controller[i].id[0] ||
                changed.controller[i].player == X2_SETTINGS_UNASSIGNED) {
                slot = (int)i;
                break;
            }
    if (slot < 0) return 0;
    snprintf(changed.controller[slot].id,
             sizeof changed.controller[slot].id, "%s", id);
    changed.controller[slot].player = (int8_t)player;
    if (player > 0) clear_player_keyboards(&changed, (unsigned)player);
    if (!x2_settings_input_assignments_valid(&changed)) return 0;
    *settings = changed;
    return 1;
}

int x2_settings_controller_player(const X2Settings *settings, const char *id)
{
    int slot = settings ? controller_slot(settings, id) : -1;
    return slot >= 0 ? settings->controller[slot].player
                     : X2_SETTINGS_UNASSIGNED;
}

const char *x2_settings_player_controller(const X2Settings *settings,
                                          unsigned player)
{
    unsigned i;
    if (!settings || player >= X2_SETTINGS_PLAYERS) return NULL;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if (settings->controller[i].player == (int)player)
            return settings->controller[i].id;
    return NULL;
}

int x2_settings_player_keyboard(const X2Settings *settings, unsigned player)
{
    unsigned i;
    if (!settings || player >= X2_SETTINGS_PLAYERS) return -1;
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
        if (settings->keyboard_player[i] == (int)player) return (int)i;
    return -1;
}
