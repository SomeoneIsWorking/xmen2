#include "continue_policy.h"

#include <string.h>

typedef struct {
  X2MainMenuText text;
  unsigned source;
} MenuRow;

enum { SHIPPED_REVIEW_ROW = 3u, SHIPPED_ONLINE_ROW = 5u, CANDIDATE_ROWS = 8u };

static unsigned without(MenuRow *rows, unsigned count, unsigned source) {
  unsigned kept = 0;
  for (unsigned i = 0; i < count; i++) {
    if (rows[i].source != source) {
      rows[kept++] = rows[i];
    }
  }
  return kept;
}

void x2_continue_menu_plan(int has_save, int has_lan_game,
                           X2ContinueMenuPlan *out) {
  MenuRow rows[CANDIDATE_ROWS];
  unsigned count = 0;
  unsigned row;

  if (!out)
    return;
  if (has_lan_game)
    rows[count++] = (MenuRow){X2_MENU_TEXT_JOIN_LAN, X2_MENU_COMMAND_JOIN_LAN};
  if (has_save)
    rows[count++] = (MenuRow){X2_MENU_TEXT_CONTINUE, X2_MENU_COMMAND_CONTINUE};
  for (row = 0; row < X2_MAIN_MENU_ROWS; row++)
    rows[count++] =
        (MenuRow){(X2MainMenuText)(X2_MENU_TEXT_NEW_GAME + row), row};
  if (count > X2_MAIN_MENU_ROWS)
    count = without(rows, count, SHIPPED_ONLINE_ROW);
  if (count > X2_MAIN_MENU_ROWS)
    count = without(rows, count, SHIPPED_REVIEW_ROW);

  memset(out, 0, sizeof *out);
  out->show_last_row = 1;
  for (row = 0; row < X2_MAIN_MENU_ROWS; row++) {
    out->text[row] = rows[row].text;
    out->command_source[row] = rows[row].source;
    if (rows[row].text == X2_MENU_TEXT_DANGER_ROOM)
      out->danger_row = row;
  }
  out->disable_online_special =
      rows[X2_MAIN_MENU_ROWS - 1u].source != SHIPPED_ONLINE_ROW;
}

int x2_continue_leaf_slot(const char *leaf, unsigned *slot) {
  if (!leaf || !slot)
    return 0;
  if (!strcmp(leaf, "autosave.save")) {
    *slot = 0u;
    return 1;
  }
  if (strlen(leaf) == 14u && !strncmp(leaf, "saveslot", 8u) && leaf[8] >= '0' &&
      leaf[8] <= '9' && !strcmp(leaf + 9, ".save")) {
    *slot = (unsigned)(leaf[8] - '0');
    return 1;
  }
  return 0;
}

void x2_continue_transaction_begin(X2ContinueTransaction *transaction) {
  if (transaction)
    transaction->auto_ack_pending = 1;
}

void x2_continue_transaction_reader_result(X2ContinueTransaction *transaction,
                                           int succeeded) {
  if (transaction && !succeeded)
    transaction->auto_ack_pending = 0;
}

int x2_continue_transaction_take_success_ack(X2ContinueTransaction *transaction,
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
