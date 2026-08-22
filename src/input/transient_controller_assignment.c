#include "transient_controller_assignment.h"

#include "controller_instance.h"
#include "dinput_pad.h"

#include <stdio.h>
#include <string.h>

#define TRANSIENT_PLAYERS 4

typedef struct {
    int assigned;
    X2ControllerInstance instance;
    char id[64];
} TransientAssignment;

static TransientAssignment g_assignment[TRANSIENT_PLAYERS];

int x2_transient_controller_assign(int pad, unsigned player)
{
    unsigned char guid[16];
    const char *id;
    unsigned i;
    if (player >= TRANSIENT_PLAYERS ||
        !dinput_pad_instance_guid(pad, guid)) return 0;
    for (i = 0; i < TRANSIENT_PLAYERS; i++)
        if (g_assignment[i].assigned &&
            x2_controller_instance_matches(&g_assignment[i].instance, guid))
            memset(&g_assignment[i], 0, sizeof g_assignment[i]);
    memset(&g_assignment[player], 0, sizeof g_assignment[player]);
    g_assignment[player].assigned = 1;
    x2_controller_instance_bind(&g_assignment[player].instance, guid);
    id = dinput_pad_persistent_id(pad);
    snprintf(g_assignment[player].id, sizeof g_assignment[player].id,
             "%s", id ? id : "session-controller");
    return 1;
}

void x2_transient_controller_clear_player(unsigned player)
{
    if (player < TRANSIENT_PLAYERS)
        memset(&g_assignment[player], 0, sizeof g_assignment[player]);
}

int x2_transient_controller_has_assignment(unsigned player)
{
    return player < TRANSIENT_PLAYERS && g_assignment[player].assigned;
}

int x2_transient_controller_resolve(unsigned player)
{
    return x2_transient_controller_has_assignment(player)
        ? x2_controller_instance_resolve(&g_assignment[player].instance) : -1;
}

int x2_transient_controller_player_for_pad(int pad)
{
    unsigned char guid[16];
    unsigned player;
    if (!dinput_pad_instance_guid(pad, guid)) return -1;
    for (player = 0; player < TRANSIENT_PLAYERS; player++)
        if (g_assignment[player].assigned &&
            x2_controller_instance_matches(&g_assignment[player].instance, guid))
            return (int)player;
    return -1;
}

const char *x2_transient_controller_id(unsigned player)
{
    return x2_transient_controller_has_assignment(player)
        ? g_assignment[player].id : NULL;
}

void x2_transient_controller_reset(void)
{
    memset(g_assignment, 0, sizeof g_assignment);
}
