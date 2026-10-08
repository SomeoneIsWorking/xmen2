#include "input_assignments.h"

#include <stdio.h>
#include <string.h>

static int player_has_keyboard(const X2Settings *settings, unsigned player) {
  unsigned i;
  for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
    if (settings->keyboard_player[i] == (int)player)
      return 1;
  return 0;
}

static int player_has_controller(const X2Settings *settings, unsigned player) {
  unsigned i;
  for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
    if (settings->controller[i].player == (int)player)
      return 1;
  return 0;
}

namespace x2::config {

int input_owner_valid(int player) {
  return player == X2_SETTINGS_UNASSIGNED ||
         (player >= 0 && player < (int)X2_SETTINGS_PLAYERS);
}

int input_assignments_valid(const X2Settings *settings) {
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

} // namespace x2::config

static void clear_controller_slot(X2Settings *settings, unsigned slot) {
  memset(&settings->controller[slot], 0, sizeof settings->controller[slot]);
  settings->controller[slot].player = X2_SETTINGS_UNASSIGNED;
}

/* P1 holds at most one device of each kind; P2-P4 hold one device. */
static int seat_accepts(const X2Settings *settings, int seat, int keyboard) {
  if (seat < 0)
    return 1;
  if (seat == 0)
    return keyboard ? !player_has_keyboard(settings, 0u)
                    : !player_has_controller(settings, 0u);
  return !player_has_keyboard(settings, (unsigned)seat) &&
         !player_has_controller(settings, (unsigned)seat);
}

static int keyboard_at(const X2Settings *settings, int seat, int except) {
  unsigned i;
  for (i = 0; seat >= 0 && i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
    if ((int)i != except && settings->keyboard_player[i] == seat)
      return (int)i;
  return -1;
}

static int controller_at(const X2Settings *settings, int seat, int except) {
  unsigned i;
  for (i = 0; seat >= 0 && i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
    if ((int)i != except && settings->controller[i].player == seat)
      return (int)i;
  return -1;
}

/* The device a move displaces takes the seat the moved device left. */
static void relocate_displaced(X2Settings *settings, int keyboard,
                               int controller, int vacated) {
  if (keyboard >= 0) {
    settings->keyboard_player[keyboard] = X2_SETTINGS_UNASSIGNED;
    if (seat_accepts(settings, vacated, 1))
      settings->keyboard_player[keyboard] = (int8_t)vacated;
  }
  if (controller >= 0) {
    settings->controller[controller].player = X2_SETTINGS_UNASSIGNED;
    if (vacated >= 0 && seat_accepts(settings, vacated, 0))
      settings->controller[controller].player = (int8_t)vacated;
    else
      clear_controller_slot(settings, (unsigned)controller);
  }
}

int x2_settings_assign_keyboard(X2Settings *settings, unsigned profile,
                                int player) {
  X2Settings changed;
  int vacated, displaced_keyboard, displaced_controller;
  if (!settings || profile >= X2_SETTINGS_KEYBOARD_PROFILES ||
      !x2::config::input_owner_valid(player))
    return 0;
  changed = *settings;
  vacated = changed.keyboard_player[profile];
  displaced_keyboard = keyboard_at(&changed, player, (int)profile);
  displaced_controller = player > 0 ? controller_at(&changed, player, -1) : -1;
  changed.keyboard_player[profile] = (int8_t)player;
  if (vacated != player)
    relocate_displaced(&changed, displaced_keyboard, displaced_controller,
                       vacated);
  if (!x2::config::input_assignments_valid(&changed))
    return 0;
  *settings = changed;
  return 1;
}

static int controller_slot(const X2Settings *settings, const char *id) {
  unsigned i;
  if (!id || !id[0])
    return -1;
  for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
    if (strcmp(settings->controller[i].id, id) == 0)
      return (int)i;
  return -1;
}

static int free_controller_slot(const X2Settings *settings) {
  unsigned i;
  for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
    if (!settings->controller[i].id[0] ||
        settings->controller[i].player == X2_SETTINGS_UNASSIGNED)
      return (int)i;
  return -1;
}

int x2_settings_assign_controller(X2Settings *settings, const char *id,
                                  int player) {
  X2Settings changed;
  int slot, vacated, displaced_keyboard, displaced_controller;
  if (!settings || !id || !id[0] || strlen(id) >= X2_SETTINGS_DEVICE_ID ||
      !x2::config::input_owner_valid(player))
    return 0;
  changed = *settings;
  slot = controller_slot(&changed, id);
  vacated =
      slot >= 0 ? changed.controller[slot].player : X2_SETTINGS_UNASSIGNED;
  if (player == X2_SETTINGS_UNASSIGNED) {
    if (slot >= 0)
      clear_controller_slot(&changed, (unsigned)slot);
  } else {
    displaced_controller = controller_at(&changed, player, slot);
    displaced_keyboard = player > 0 ? keyboard_at(&changed, player, -1) : -1;
    if (slot < 0) {
      slot = free_controller_slot(&changed);
      if (slot < 0 && displaced_controller >= 0) {
        clear_controller_slot(&changed, (unsigned)displaced_controller);
        slot = displaced_controller;
        displaced_controller = -1;
      }
      if (slot < 0)
        return 0;
    }
    snprintf(changed.controller[slot].id, sizeof changed.controller[slot].id,
             "%s", id);
    changed.controller[slot].player = (int8_t)player;
    if (vacated != player)
      relocate_displaced(&changed, displaced_keyboard, displaced_controller,
                         vacated);
  }
  if (!x2::config::input_assignments_valid(&changed))
    return 0;
  *settings = changed;
  return 1;
}

int x2_settings_controller_player(const X2Settings *settings, const char *id) {
  int slot = settings ? controller_slot(settings, id) : -1;
  return slot >= 0 ? settings->controller[slot].player : X2_SETTINGS_UNASSIGNED;
}

const char *x2_settings_player_controller(const X2Settings *settings,
                                          unsigned player) {
  unsigned i;
  if (!settings || player >= X2_SETTINGS_PLAYERS)
    return NULL;
  for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
    if (settings->controller[i].player == (int)player)
      return settings->controller[i].id;
  return NULL;
}

int x2_settings_player_keyboard(const X2Settings *settings, unsigned player) {
  unsigned i;
  if (!settings || player >= X2_SETTINGS_PLAYERS)
    return -1;
  for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
    if (settings->keyboard_player[i] == (int)player)
      return (int)i;
  return -1;
}
