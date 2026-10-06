#pragma once

#include <cstddef>

namespace x2::save {

inline constexpr unsigned kMainMenuRows = 6u;

enum class MainMenuText : int {
  Continue,
  NewGame,
  LoadGame,
  DangerRoom,
  Review,
  Options,
  PlayOnline,
  /* "Join <host>": the port's LAN row, its text supplied at runtime. */
  JoinLan
};

/* The shipped rows' texts, Continue through Play Online. */
inline constexpr unsigned kShippedMenuTexts =
    static_cast<unsigned>(MainMenuText::PlayOnline) + 1u;

/* command_source values beyond the six shipped rows. */
inline constexpr unsigned kMenuCommandContinue = 6u;
inline constexpr unsigned kMenuCommandJoinLan = 7u;

struct ContinueMenuPlan {
  MainMenuText text[kMainMenuRows];
  unsigned command_source[kMainMenuRows];
  int show_last_row;
  unsigned danger_row;
  int disable_online_special;
};

struct ContinueTransaction {
  int auto_ack_pending;
};

/* command_source is an original shipped row index (0..5), or one of the
   kMenuCommand* port commands. The rows are, in order, Join (when a LAN
   game is announced), Continue (with a save), then the shipped rows; while
   more than six remain, Play Online leaves first and Review second. The plan
   never depends on previously-mutated menu state, so repeated Show calls
   cannot progressively shift the rows. */
void continue_menu_plan(int has_save, int has_lan_game, ContinueMenuPlan *out);

/* Choose the retail metadata staging record for an exact catalog leaf.
   Manual leaves keep their authored slot; autosave uses record zero only as
   temporary metadata while the one-shot leaf redirect owns the actual read. */
int continue_leaf_slot(const char *leaf, unsigned *slot);

/* Native Continue is the only load source that arms success-dialog
   acknowledgement. A failed payload read or an unexpected completion state
   consumes the one-shot so a later manual Load can never inherit it. */
void continue_transaction_begin(ContinueTransaction *transaction);
void continue_transaction_reader_result(ContinueTransaction *transaction,
                                        int succeeded);
int continue_transaction_take_success_ack(ContinueTransaction *transaction,
                                          unsigned manager_mode,
                                          unsigned manager_state);

} // namespace x2::save
