#include "input_assignments.h"

#include <stdio.h>
#include <string.h>

namespace x2::config {

namespace {
int player_has_keyboard(const Settings *settings, unsigned player) {
  unsigned i;
  for (i = 0; i < kSettingsKeyboardProfiles; i++)
    if (settings->keyboard_player[i] == (int)player)
      return 1;
  return 0;
}

int player_has_controller(const Settings *settings, unsigned player) {
  unsigned i;
  for (i = 0; i < kSettingsControllerAssignments; i++)
    if (settings->controller[i].player == (int)player)
      return 1;
  return 0;
}
} // namespace

int input_owner_valid(int player) {
  return player == kSettingsUnassigned ||
         (player >= 0 && player < (int)kSettingsPlayers);
}

int input_assignments_valid(const Settings *settings) {
  unsigned player;
  if (!settings || (!player_has_keyboard(settings, 0u) &&
                    !player_has_controller(settings, 0u)))
    return 0;
  for (player = 1; player < kSettingsPlayers; player++)
    if (player_has_keyboard(settings, player) &&
        player_has_controller(settings, player))
      return 0;
  return 1;
}

namespace {
void clear_controller_slot(Settings *settings, unsigned slot) {
  memset(&settings->controller[slot], 0, sizeof settings->controller[slot]);
  settings->controller[slot].player = kSettingsUnassigned;
}

/* P1 holds at most one device of each kind; P2-P4 hold one device. */
int seat_accepts(const Settings *settings, int seat, int keyboard) {
  if (seat < 0)
    return 1;
  if (seat == 0)
    return keyboard ? !player_has_keyboard(settings, 0u)
                    : !player_has_controller(settings, 0u);
  return !player_has_keyboard(settings, (unsigned)seat) &&
         !player_has_controller(settings, (unsigned)seat);
}

int keyboard_at(const Settings *settings, int seat, int except) {
  unsigned i;
  for (i = 0; seat >= 0 && i < kSettingsKeyboardProfiles; i++)
    if ((int)i != except && settings->keyboard_player[i] == seat)
      return (int)i;
  return -1;
}

int controller_at(const Settings *settings, int seat, int except) {
  unsigned i;
  for (i = 0; seat >= 0 && i < kSettingsControllerAssignments; i++)
    if ((int)i != except && settings->controller[i].player == seat)
      return (int)i;
  return -1;
}

/* The device a move displaces takes the seat the moved device left. */
void relocate_displaced(Settings *settings, int keyboard, int controller,
                        int vacated) {
  if (keyboard >= 0) {
    settings->keyboard_player[keyboard] = kSettingsUnassigned;
    if (seat_accepts(settings, vacated, 1))
      settings->keyboard_player[keyboard] = (int8_t)vacated;
  }
  if (controller >= 0) {
    settings->controller[controller].player = kSettingsUnassigned;
    if (vacated >= 0 && seat_accepts(settings, vacated, 0))
      settings->controller[controller].player = (int8_t)vacated;
    else
      clear_controller_slot(settings, (unsigned)controller);
  }
}
} // namespace

int settings_assign_keyboard(Settings *settings, unsigned profile, int player) {
  Settings changed;
  int vacated, displaced_keyboard, displaced_controller;
  if (!settings || profile >= kSettingsKeyboardProfiles ||
      !input_owner_valid(player))
    return 0;
  changed = *settings;
  vacated = changed.keyboard_player[profile];
  displaced_keyboard = keyboard_at(&changed, player, (int)profile);
  displaced_controller = player > 0 ? controller_at(&changed, player, -1) : -1;
  changed.keyboard_player[profile] = (int8_t)player;
  if (vacated != player)
    relocate_displaced(&changed, displaced_keyboard, displaced_controller,
                       vacated);
  if (!input_assignments_valid(&changed))
    return 0;
  *settings = changed;
  return 1;
}

namespace {
int controller_slot(const Settings *settings, const char *id) {
  unsigned i;
  if (!id || !id[0])
    return -1;
  for (i = 0; i < kSettingsControllerAssignments; i++)
    if (strcmp(settings->controller[i].id, id) == 0)
      return (int)i;
  return -1;
}

int free_controller_slot(const Settings *settings) {
  unsigned i;
  for (i = 0; i < kSettingsControllerAssignments; i++)
    if (!settings->controller[i].id[0] ||
        settings->controller[i].player == kSettingsUnassigned)
      return (int)i;
  return -1;
}
} // namespace

int settings_assign_controller(Settings *settings, const char *id, int player) {
  Settings changed;
  int slot, vacated, displaced_keyboard, displaced_controller;
  if (!settings || !id || !id[0] || strlen(id) >= kSettingsDeviceId ||
      !input_owner_valid(player))
    return 0;
  changed = *settings;
  slot = controller_slot(&changed, id);
  vacated = slot >= 0 ? changed.controller[slot].player : kSettingsUnassigned;
  if (player == kSettingsUnassigned) {
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
  if (!input_assignments_valid(&changed))
    return 0;
  *settings = changed;
  return 1;
}

int settings_controller_player(const Settings *settings, const char *id) {
  int slot = settings ? controller_slot(settings, id) : -1;
  return slot >= 0 ? settings->controller[slot].player : kSettingsUnassigned;
}

const char *settings_player_controller(const Settings *settings,
                                       unsigned player) {
  unsigned i;
  if (!settings || player >= kSettingsPlayers)
    return NULL;
  for (i = 0; i < kSettingsControllerAssignments; i++)
    if (settings->controller[i].player == (int)player)
      return settings->controller[i].id;
  return NULL;
}

int settings_player_keyboard(const Settings *settings, unsigned player) {
  unsigned i;
  if (!settings || player >= kSettingsPlayers)
    return -1;
  for (i = 0; i < kSettingsKeyboardProfiles; i++)
    if (settings->keyboard_player[i] == (int)player)
      return (int)i;
  return -1;
}

} // namespace x2::config
