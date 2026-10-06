#include "continue_policy.h"

#include <cstring>

namespace x2::save {

namespace {

struct MenuRow {
  MainMenuText text;
  unsigned source;
};

enum { SHIPPED_REVIEW_ROW = 3u, SHIPPED_ONLINE_ROW = 5u, CANDIDATE_ROWS = 8u };

unsigned without(MenuRow *rows, unsigned count, unsigned source) {
  unsigned kept = 0;
  for (unsigned i = 0; i < count; i++) {
    if (rows[i].source != source) {
      rows[kept++] = rows[i];
    }
  }
  return kept;
}

} // namespace

void continue_menu_plan(int has_save, int has_lan_game, ContinueMenuPlan *out) {
  MenuRow rows[CANDIDATE_ROWS];
  unsigned count = 0;
  unsigned row;

  if (!out)
    return;
  if (has_lan_game)
    rows[count++] = (MenuRow){MainMenuText::JoinLan, kMenuCommandJoinLan};
  if (has_save)
    rows[count++] = (MenuRow){MainMenuText::Continue, kMenuCommandContinue};
  for (row = 0; row < kMainMenuRows; row++)
    rows[count++] =
        (MenuRow){static_cast<MainMenuText>(
                      static_cast<unsigned>(MainMenuText::NewGame) + row),
                  row};
  if (count > kMainMenuRows)
    count = without(rows, count, SHIPPED_ONLINE_ROW);
  if (count > kMainMenuRows)
    count = without(rows, count, SHIPPED_REVIEW_ROW);

  std::memset(out, 0, sizeof *out);
  out->show_last_row = 1;
  for (row = 0; row < kMainMenuRows; row++) {
    out->text[row] = rows[row].text;
    out->command_source[row] = rows[row].source;
    if (rows[row].text == MainMenuText::DangerRoom)
      out->danger_row = row;
  }
  out->disable_online_special =
      rows[kMainMenuRows - 1u].source != SHIPPED_ONLINE_ROW;
}

int continue_leaf_slot(const char *leaf, unsigned *slot) {
  if (!leaf || !slot)
    return 0;
  if (!std::strcmp(leaf, "autosave.save")) {
    *slot = 0u;
    return 1;
  }
  if (std::strlen(leaf) == 14u && !std::strncmp(leaf, "saveslot", 8u) &&
      leaf[8] >= '0' && leaf[8] <= '9' && !std::strcmp(leaf + 9, ".save")) {
    *slot = (unsigned)(leaf[8] - '0');
    return 1;
  }
  return 0;
}

void continue_transaction_begin(ContinueTransaction *transaction) {
  if (transaction)
    transaction->auto_ack_pending = 1;
}

void continue_transaction_reader_result(ContinueTransaction *transaction,
                                        int succeeded) {
  if (transaction && !succeeded)
    transaction->auto_ack_pending = 0;
}

int continue_transaction_take_success_ack(ContinueTransaction *transaction,
                                          unsigned manager_mode,
                                          unsigned manager_state) {
  int acknowledge;
  if (!transaction)
    return 0;
  acknowledge = transaction->auto_ack_pending && manager_mode == 3u &&
                manager_state == 1u;
  transaction->auto_ack_pending = 0;
  return acknowledge;
}

} // namespace x2::save
