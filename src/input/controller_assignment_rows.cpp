#include "controller_assignment_rows.hpp"

#include <algorithm>

#include "dinput_pad.h"
#include "settings_store.h"
#include "transient_controller_assignment.h"

namespace x2::input {
namespace {

bool save(std::string *why) {
  char reason[256] = "";
  if (x2::config::settings_store_save(reason,
                                      static_cast<int>(sizeof reason))) {
    return true;
  }
  *why = reason;
  return false;
}

/* A session pad displaced from `seat` takes the seat its displacer left. */
void relocate_session_pad(int seat, int vacated) {
  if (seat < 0 ||
      !transient_controller_has_assignment(static_cast<unsigned>(seat))) {
    return;
  }
  if (vacated < 0 || vacated == seat ||
      !transient_controller_move(static_cast<unsigned>(seat),
                                 static_cast<unsigned>(vacated))) {
    transient_controller_clear_player(static_cast<unsigned>(seat));
  }
}

} // namespace

std::vector<ControllerAssignmentRow>
controller_assignment_rows(const x2::config::Settings &settings) {
  std::vector<ControllerAssignmentRow> rows;
  for (unsigned player = 0; player < x2::config::kSettingsPlayers; player++) {
    const char *id = transient_controller_id(player);
    if (id == nullptr) {
      continue;
    }
    const int pad = transient_controller_resolve(player);
    const char *name = pad >= 0 ? dinput_pad_name(pad) : nullptr;
    rows.push_back({id, name != nullptr ? name : "", false, true, pad,
                    static_cast<int>(player)});
  }
  for (const auto &saved : settings.controller) {
    const char *id = saved.id;
    if (id[0] == '\0') {
      continue;
    }
    if (std::any_of(rows.begin(), rows.end(),
                    [id](const ControllerAssignmentRow &row) {
                      return row.id == id;
                    })) {
      continue;
    }
    int owner = saved.player;
    if (owner >= 0 &&
        transient_controller_has_assignment(static_cast<unsigned>(owner))) {
      owner = -2;
    }
    rows.push_back({id, "", true, false, -1, owner});
  }
  for (int pad = 0; pad < DINPUT_PAD_MAX; pad++) {
    const char *id = dinput_pad_persistent_id(pad);
    const char *name = dinput_pad_name(pad);
    if (id == nullptr || name == nullptr) {
      continue;
    }
    auto found = std::find_if(
        rows.begin(), rows.end(),
        [id](const ControllerAssignmentRow &row) { return row.id == id; });
    if (found == rows.end()) {
      rows.push_back({id, name, dinput_pad_persistent_id_is_stable(pad) != 0,
                      false, pad, transient_controller_player_for_pad(pad)});
    } else {
      found->name = name;
      found->stable_identity = dinput_pad_persistent_id_is_stable(pad) != 0;
      found->pad = pad;
    }
  }
  return rows;
}

bool assign_keyboard(x2::config::Settings &settings, unsigned profile,
                     int owner, std::string *why) {
  if (profile >= x2::config::kSettingsKeyboardProfiles) {
    *why = "no such keyboard";
    return false;
  }
  const int vacated = settings.keyboard_player[profile];
  if (!settings_assign_keyboard(&settings, profile, owner)) {
    *why = "the assignment was refused";
    return false;
  }
  /* Player 1 may hold a keyboard and a pad together. */
  if (owner > 0) {
    relocate_session_pad(owner, vacated);
  }
  return save(why);
}

bool assign_controller(x2::config::Settings &settings,
                       const ControllerAssignmentRow &row, int owner,
                       std::string *why) {
  if (row.transient_assignment || !row.stable_identity) {
    if (owner < 0) {
      if (row.owner >= 0) {
        transient_controller_clear_player(static_cast<unsigned>(row.owner));
      }
      return true;
    }
    if (row.pad < 0 ||
        !transient_controller_assign(row.pad, static_cast<unsigned>(owner))) {
      *why = "the controller is not connected";
      return false;
    }
    return true;
  }
  if (!settings_assign_controller(&settings, row.id.c_str(), owner)) {
    *why = "the assignment was refused";
    return false;
  }
  relocate_session_pad(owner, row.owner);
  return save(why);
}

} // namespace x2::input
