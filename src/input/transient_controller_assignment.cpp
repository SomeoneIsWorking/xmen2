#include "transient_controller_assignment.h"

#include "controller_instance.h"
#include "dinput_pad.h"

#include <stdio.h>
#include <string.h>

namespace x2::input {

#define TRANSIENT_PLAYERS 4

typedef struct {
  int assigned;
  x2::input::ControllerInstance instance;
  char id[64];
} TransientAssignment;

static TransientAssignment g_assignment[TRANSIENT_PLAYERS];

int transient_controller_assign(int pad, unsigned player) {
  unsigned char guid[16];
  TransientAssignment displaced;
  const char *id;
  int vacated = -1;
  unsigned i;
  if (player >= TRANSIENT_PLAYERS || !dinput_pad_instance_guid(pad, guid))
    return 0;
  displaced = g_assignment[player];
  for (i = 0; i < TRANSIENT_PLAYERS; i++)
    if (g_assignment[i].assigned && x2::input::controller_instance_matches(
                                        &g_assignment[i].instance, guid)) {
      memset(&g_assignment[i], 0, sizeof g_assignment[i]);
      vacated = (int)i;
    }
  memset(&g_assignment[player], 0, sizeof g_assignment[player]);
  g_assignment[player].assigned = 1;
  x2::input::controller_instance_bind(&g_assignment[player].instance, guid);
  id = dinput_pad_persistent_id(pad);
  snprintf(g_assignment[player].id, sizeof g_assignment[player].id, "%s",
           id ? id : "session-controller");
  if (displaced.assigned && vacated >= 0 && vacated != (int)player)
    g_assignment[vacated] = displaced;
  return 1;
}

int transient_controller_move(unsigned from, unsigned to) {
  if (from >= TRANSIENT_PLAYERS || to >= TRANSIENT_PLAYERS ||
      !g_assignment[from].assigned || g_assignment[to].assigned)
    return 0;
  g_assignment[to] = g_assignment[from];
  memset(&g_assignment[from], 0, sizeof g_assignment[from]);
  return 1;
}

void transient_controller_clear_player(unsigned player) {
  if (player < TRANSIENT_PLAYERS)
    memset(&g_assignment[player], 0, sizeof g_assignment[player]);
}

int transient_controller_has_assignment(unsigned player) {
  return player < TRANSIENT_PLAYERS && g_assignment[player].assigned;
}

int transient_controller_resolve(unsigned player) {
  return transient_controller_has_assignment(player)
             ? x2::input::controller_instance_resolve(
                   &g_assignment[player].instance)
             : -1;
}

int transient_controller_player_for_pad(int pad) {
  unsigned char guid[16];
  unsigned player;
  if (!dinput_pad_instance_guid(pad, guid))
    return -1;
  for (player = 0; player < TRANSIENT_PLAYERS; player++)
    if (g_assignment[player].assigned &&
        x2::input::controller_instance_matches(&g_assignment[player].instance,
                                               guid))
      return (int)player;
  return -1;
}

const char *transient_controller_id(unsigned player) {
  return transient_controller_has_assignment(player) ? g_assignment[player].id
                                                     : NULL;
}

void transient_controller_reset(void) {
  memset(g_assignment, 0, sizeof g_assignment);
}

} // namespace x2::input
