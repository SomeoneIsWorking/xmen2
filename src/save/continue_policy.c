#include "continue_policy.h"

#include <string.h>

void x2_continue_menu_plan(int has_save, X2ContinueMenuPlan *out) {
  static const X2ContinueMenuPlan WITHOUT_SAVE = {
      {X2_MENU_TEXT_NEW_GAME, X2_MENU_TEXT_LOAD_GAME, X2_MENU_TEXT_DANGER_ROOM,
       X2_MENU_TEXT_REVIEW, X2_MENU_TEXT_OPTIONS, X2_MENU_TEXT_PLAY_ONLINE},
      {0u, 1u, 2u, 3u, 4u, 5u},
      0,
      2u,
      0};
  static const X2ContinueMenuPlan WITH_SAVE = {
      {X2_MENU_TEXT_CONTINUE, X2_MENU_TEXT_NEW_GAME, X2_MENU_TEXT_LOAD_GAME,
       X2_MENU_TEXT_DANGER_ROOM, X2_MENU_TEXT_REVIEW, X2_MENU_TEXT_OPTIONS},
      {6u, 0u, 1u, 2u, 3u, 4u},
      1,
      3u,
      1};

  if (out)
    *out = has_save ? WITH_SAVE : WITHOUT_SAVE;
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
