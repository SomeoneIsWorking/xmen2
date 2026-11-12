#ifndef X2_CONTROLLER_ASSIGNMENT_ROWS_HPP
#define X2_CONTROLLER_ASSIGNMENT_ROWS_HPP

#include <string>
#include <vector>

#include "settings.h"

namespace x2::input {

/* One controller the player can assign: connected, saved, or assigned for
   this session only. */
struct ControllerAssignmentRow {
  std::string id;
  /* The pad's own name, or empty while it is disconnected. */
  std::string name;
  bool stable_identity;
  /* Assigned for this session only; the saved assignment is overridden. */
  bool transient_assignment;
  int pad;
  /* The player, -1 none, -2 a saved owner displaced for this session. */
  int owner;
};

std::vector<ControllerAssignmentRow>
controller_assignment_rows(const x2::config::Settings &settings);

/* Gives keyboard `profile` to `owner` (-1 none) and saves; a session pad on
   that seat moves to the seat the keyboard left. False with `why` filled
   when nothing changed. */
bool assign_keyboard(x2::config::Settings &settings, unsigned profile,
                     int owner, std::string *why);
/* Gives `row`'s controller to `owner` (-1 none): saved when its identity is
   stable, for this session otherwise. */
bool assign_controller(x2::config::Settings &settings,
                       const ControllerAssignmentRow &row, int owner,
                       std::string *why);

} // namespace x2::input

#endif
