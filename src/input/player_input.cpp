#include "player_input.h"
#include "../native/x2_log.h"

#include "dinput8_controller_slots.h"
#include "dinput_pad.h"
#include "input_bindings.h"
#include "player_participation.h"
#include "player_participation_policy.h"
#include "settings_store.h"
#include "transient_controller_assignment.h"
#include "x86rt.h"
#include "xbox_defaults.h"

#include <stdio.h>
#include <string.h>

namespace x2::input {

namespace {
typedef struct {
  uint32_t kind;
  uint32_t code;
} Binding;

_Static_assert(x2::config::kSettingsRows == INPUT_BINDING_ROWS,
               "settings profiles must cover every shipping binding row");

Binding g_keyboard_base[INPUT_BINDING_ROWS];
x2::config::Settings g_last;
int g_have_base;
int g_have_last;
int g_last_pad[INPUT_PLAYERS] = {-2, -2, -2, -2};
int g_last_controller_slot[INPUT_PLAYERS] = {-2, -2, -2, -2};
int g_last_keyboard[INPUT_PLAYERS] = {-2, -2, -2, -2};
int g_last_source_gamepad[INPUT_PLAYERS];
unsigned char g_last_keyboard_state[256];
x2::input::PlayerParticipationPolicy g_participation;
int g_have_participation;

#define PAUSE_ROW 17u
#define DI_JOYSTATE_BUTTONS 48u

int capture_keyboard_base(void) {
  unsigned row;
  char why[192];
  uint32_t object =
      input_bindings_object_at(INPUT_SET_MASTER, why, (int)sizeof why);
  if (!object)
    return 0;
  for (row = 0; row < INPUT_BINDING_ROWS; row++)
    if (!input_bindings_read(object, row, 0, &g_keyboard_base[row].kind,
                             &g_keyboard_base[row].code))
      return 0;
  g_have_base = 1;
  return 1;
}

void resolve_pads(const x2::config::Settings *settings,
                  int out[INPUT_PLAYERS]) {
  int claimed[DINPUT_PAD_MAX] = {0};
  unsigned player;
  int pad;

  for (player = 0; player < INPUT_PLAYERS; player++)
    out[player] = -1;
  /* A process-lifetime assignment names one exact live instance. It wins
     over persisted reservations even while disconnected, so slot reuse or
     a different stored controller cannot make the player roam. */
  for (player = 0; player < INPUT_PLAYERS; player++) {
    if (x2::input::transient_controller_has_assignment(player)) {
      pad = x2::input::transient_controller_resolve(player);
      if (pad >= 0 && !claimed[pad]) {
        out[player] = pad;
        claimed[pad] = 1;
      }
    }
  }
  for (player = 0; player < INPUT_PLAYERS; player++) {
    const char *id = settings_player_controller(settings, player);
    if (x2::input::transient_controller_has_assignment(player))
      continue;
    if (!id)
      continue;
    pad = dinput_pad_for_persistent_id(id);
    if (pad >= 0 && !claimed[pad]) {
      out[player] = pad;
      claimed[pad] = 1;
    }
  }
}

uint32_t default_gamepad_code(unsigned row) {
  const XboxDefaultBinding *defaults;
  size_t count, i;
  defaults = xbox_default_bindings(&count);
  for (i = 0; i < count; i++)
    if (defaults[i].binding == row)
      return defaults[i].code;
  return 0;
}

int keyboard_code(const x2::config::Settings *settings, int profile_index,
                  unsigned row, uint32_t *code) {
  const x2::config::KeyboardProfile *profile;
  uint32_t kind;

  if (!settings || profile_index < 0 || row >= INPUT_BINDING_ROWS)
    return 0;
  profile = &settings->keyboard_profile[profile_index];
  kind = g_keyboard_base[row].kind;
  *code = g_keyboard_base[row].code;
  if (profile->keyboard_set[row]) {
    kind = profile->keyboard[row] ? 1u : 0u;
    *code = profile->keyboard[row];
  }
  return kind == 1u;
}

uint8_t eligibility_mask(const x2::config::Settings *settings,
                         const int keyboard[INPUT_PLAYERS]) {
  uint8_t eligible = 0;
  unsigned player;
  for (player = 0; player < INPUT_PLAYERS; player++)
    if (keyboard[player] >= 0 ||
        x2::input::transient_controller_has_assignment(player) ||
        settings_player_controller(settings, player))
      eligible |= (uint8_t)(1u << player);
  return eligible;
}

void sync_participation(CPU *cpu, const x2::config::Settings *settings,
                        const int keyboard[INPUT_PLAYERS]) {
  x2::input::PlayerParticipationTransition transition;
  uint8_t eligible;
  if (!g_have_participation) {
    x2::input::player_participation_policy_init(&g_participation);
    g_have_participation = 1;
  }
  eligible = eligibility_mask(settings, keyboard);
  x2::input::player_participation_policy_configure(&g_participation, eligible);
  transition = x2::input::player_participation_policy_consume(&g_participation);
  x2::native::player_participation_apply(cpu, transition.join,
                                         transition.leave);
  x2::native::player_participation_enforce_eligibility(cpu, eligible);
}

void publish_player(CPU *cpu, const x2::config::Settings *settings,
                    unsigned player, int keyboard_profile,
                    int controller_slot) {
  const x2::config::KeyboardProfile *profile =
      keyboard_profile >= 0 ? &settings->keyboard_profile[keyboard_profile]
                            : NULL;
  unsigned row;
  uint32_t pad_kind = controller_slot < 0 ? 0u : 3u + (uint32_t)controller_slot;

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
    input_bindings_write_player(cpu, player, row, INPUT_BINDING_ALT_SLOT,
                                pad_code ? pad_kind : 0u, pad_code);
  }
}
} // namespace

void player_input_sync(CPU *cpu) {
  x2::config::Settings *settings;
  int pad[INPUT_PLAYERS];
  int controller_slot[INPUT_PLAYERS];
  int keyboard[INPUT_PLAYERS];
  unsigned player;
  int changed;

  if (!cpu)
    return;
  dinput_pad_refresh();
  if (!g_have_base && !capture_keyboard_base())
    return;
  settings = x2::config::settings_store();
  resolve_pads(settings, pad);
  for (player = 0; player < INPUT_PLAYERS; player++) {
    controller_slot[player] =
        pad[player] < 0 ? -1
                        : dinput8_controller_slot_for_host_pad(pad[player]);
    keyboard[player] = settings_player_keyboard(settings, player);
    if (player > 0u && x2::input::transient_controller_has_assignment(player))
      keyboard[player] = -1;
  }
  changed = !g_have_last || memcmp(&g_last, settings, sizeof g_last) != 0 ||
            memcmp(g_last_pad, pad, sizeof pad) != 0 ||
            memcmp(g_last_controller_slot, controller_slot,
                   sizeof controller_slot) != 0 ||
            memcmp(g_last_keyboard, keyboard, sizeof keyboard) != 0;
  if (!changed) {
    sync_participation(cpu, settings, keyboard);
    return;
  }
  for (player = 0; player < INPUT_PLAYERS; player++) {
    if (g_have_last && (g_last_pad[player] != pad[player] ||
                        g_last_keyboard[player] != keyboard[player]))
      x2::input::player_participation_policy_note_start(&g_participation,
                                                        player, 0);
    publish_player(cpu, settings, player, keyboard[player],
                   controller_slot[player]);
    if (pad[player] >= 0 && keyboard[player] < 0)
      g_last_source_gamepad[player] = 1;
    else if (keyboard[player] >= 0 && pad[player] < 0)
      g_last_source_gamepad[player] = 0;
  }
  g_last = *settings;
  memcpy(g_last_pad, pad, sizeof pad);
  memcpy(g_last_controller_slot, controller_slot, sizeof controller_slot);
  memcpy(g_last_keyboard, keyboard, sizeof keyboard);
  g_have_last = 1;
  sync_participation(cpu, settings, keyboard);
  x2_log_error("PLAYER-INPUT: published resolved ownership for four "
               "players (host pads %d,%d,%d,%d; guest slots "
               "%d,%d,%d,%d); each physical pad is claimed by at most "
               "one player.\n",
               pad[0], pad[1], pad[2], pad[3], controller_slot[0],
               controller_slot[1], controller_slot[2], controller_slot[3]);
}

int player_input_uses_gamepad(unsigned player) {
  return g_have_last && player < INPUT_PLAYERS &&
         g_last_controller_slot[player] >= 0 &&
         (g_last_keyboard[player] < 0 || g_last_source_gamepad[player]);
}

void player_input_note_keyboard_state(const unsigned char *state,
                                      unsigned bytes) {
  unsigned player;
  if (!state || !g_have_last)
    return;
  for (player = 0; player < INPUT_PLAYERS; player++) {
    int profile_index = g_last_keyboard[player];
    unsigned row;
    if (profile_index < 0)
      continue;
    for (row = 0; row < INPUT_BINDING_ROWS; row++) {
      const x2::config::KeyboardProfile *profile =
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
      if (!keyboard_code(&g_last, g_last_keyboard[player], PAUSE_ROW, &code) ||
          code >= bytes || code >= 256u)
        continue;
      if (!(state[code] & 0x80u)) {
        x2::input::player_participation_policy_note_start(&g_participation,
                                                          player, 0);
        continue;
      }
      if (g_last_keyboard_state[code] & 0x80u)
        continue;
      for (other = 0; other < INPUT_PLAYERS; other++) {
        uint32_t other_code;
        if (other == player || !keyboard_code(&g_last, g_last_keyboard[other],
                                              PAUSE_ROW, &other_code))
          continue;
        if (other_code == code) {
          ambiguous = 1;
          break;
        }
      }
      if (!ambiguous)
        x2::input::player_participation_policy_note_start(&g_participation,
                                                          player, 1);
    }
  }
  memcpy(g_last_keyboard_state, state,
         bytes < sizeof g_last_keyboard_state ? bytes
                                              : sizeof g_last_keyboard_state);
}

void player_input_note_gamepad_activity(int pad) {
  unsigned player;
  for (player = 0; player < INPUT_PLAYERS; player++)
    if (g_have_last && g_last_pad[player] == pad &&
        g_last_controller_slot[player] >= 0)
      g_last_source_gamepad[player] = 1;
}

void player_input_note_gamepad_state(int pad, const unsigned char *state,
                                     unsigned bytes) {
  uint32_t code = default_gamepad_code(PAUSE_ROW);
  unsigned button;
  unsigned player;
  int down;

  if (!state || pad < 0 || pad >= DINPUT_PAD_MAX || code < 0x15u)
    return;
  button = code - 0x15u;
  if (DI_JOYSTATE_BUTTONS + button >= bytes)
    return;
  down = (state[DI_JOYSTATE_BUTTONS + button] & 0x80u) != 0;
  for (player = 0; player < INPUT_PLAYERS; player++)
    if (g_have_last && g_last_pad[player] == pad)
      x2::input::player_participation_policy_note_start(&g_participation,
                                                        player, down);
}

int player_input_pad_is_active_source(int pad) {
  unsigned player;
  for (player = 0; player < INPUT_PLAYERS; player++)
    if (g_have_last && g_last_pad[player] == pad &&
        g_last_controller_slot[player] >= 0)
      return g_last_keyboard[player] < 0 || g_last_source_gamepad[player];
  return 0;
}

int player_input_game_keyboard_binding(unsigned row, uint32_t *kind,
                                       uint32_t *code) {
  if (!g_have_base || row >= INPUT_BINDING_ROWS || !kind || !code)
    return 0;
  *kind = g_keyboard_base[row].kind;
  *code = g_keyboard_base[row].code;
  return 1;
}

int player_input_resolved_pad(unsigned player) {
  return g_have_last && player < INPUT_PLAYERS ? g_last_pad[player] : -1;
}

} // namespace x2::input
