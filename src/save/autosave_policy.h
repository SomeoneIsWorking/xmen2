#pragma once

#include <cstdint>

namespace x2::save {

inline constexpr unsigned kAutosaveIdlePolls = 64u;

enum class AutosaveCheckpointKind { None = 0, MapLoad };

struct AutosaveCheckpoint {
  std::uint64_t id;
  AutosaveCheckpointKind kind;
};

enum class AutosavePollResult { Idle = 0, Deferred, AwaitingResult, Fire };

struct AutosavePolicy {
  std::uint64_t map_returns;
  std::uint64_t successful_map_returns;
  std::uint64_t scheduled;
  std::uint64_t cancelled_menu;
  std::uint64_t deferred_polls;
  std::uint64_t control_deferred_polls;
  std::uint64_t attempts;
  std::uint64_t successes;
  std::uint64_t failures;
  AutosaveCheckpoint pending;
  AutosaveCheckpoint active;
  unsigned idle_polls;
  int has_pending;
  int has_active;
};

void autosave_policy_init(AutosavePolicy *policy);
void autosave_policy_map_return(AutosavePolicy *policy, int succeeded);
void autosave_policy_menu_show(AutosavePolicy *policy);
/* One guest input poll. The checkpoint fires after kAutosaveIdlePolls
   consecutive polls in which the retail save manager is idle (mode 0) AND the
   player controls a character. A level's opening script can park the party
   out of sight under a control lock and set the flag that stops it from
   running again (Dead Zone's deadzone1.py); a snapshot taken inside that
   window restores an invisible party that no script ever moves back. */
AutosavePollResult autosave_policy_poll(AutosavePolicy *policy,
                                        std::uint32_t manager_mode,
                                        int player_controls,
                                        AutosaveCheckpoint *checkpoint);
int autosave_policy_finish(AutosavePolicy *policy, std::uint64_t checkpoint_id,
                           int succeeded);

} // namespace x2::save
