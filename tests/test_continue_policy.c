#include "continue_policy.h"

#include <stdio.h>

static int checks;
static int failures;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x);  \
    }                                                                          \
  } while (0)

int main(void) {
  X2ContinueMenuPlan plan;
  X2ContinueTransaction transaction = {0};
  unsigned slot = 99u;
  unsigned i;

  x2_continue_menu_plan(0, &plan);
  CHECK(plan.text[0] == X2_MENU_TEXT_NEW_GAME);
  CHECK(plan.text[4] == X2_MENU_TEXT_OPTIONS);
  CHECK(!plan.show_last_row);
  CHECK(plan.danger_row == 2u);
  CHECK(!plan.disable_online_special);
  for (i = 0; i < X2_MAIN_MENU_ROWS; i++)
    CHECK(plan.command_source[i] == i);

  x2_continue_menu_plan(1, &plan);
  CHECK(plan.text[0] == X2_MENU_TEXT_CONTINUE);
  CHECK(plan.text[1] == X2_MENU_TEXT_NEW_GAME);
  CHECK(plan.text[2] == X2_MENU_TEXT_LOAD_GAME);
  CHECK(plan.text[3] == X2_MENU_TEXT_DANGER_ROOM);
  CHECK(plan.text[4] == X2_MENU_TEXT_REVIEW);
  CHECK(plan.text[5] == X2_MENU_TEXT_OPTIONS);
  CHECK(plan.show_last_row);
  CHECK(plan.danger_row == 3u);
  CHECK(plan.disable_online_special);
  for (i = 0; i < X2_MAIN_MENU_ROWS; i++)
    CHECK(plan.command_source[i] == (i ? i - 1u : 6u));

  CHECK(x2_continue_leaf_slot("autosave.save", &slot) && slot == 0u);
  CHECK(x2_continue_leaf_slot("saveslot0.save", &slot) && slot == 0u);
  CHECK(x2_continue_leaf_slot("saveslot9.save", &slot) && slot == 9u);
  CHECK(!x2_continue_leaf_slot("saveslot10.save", &slot));
  CHECK(!x2_continue_leaf_slot("../saveslot0.save", &slot));
  CHECK(!x2_continue_leaf_slot(NULL, &slot));
  CHECK(!x2_continue_leaf_slot("autosave.save", NULL));

  /* Manual Load never arms native Continue's one-shot. */
  CHECK(!x2_continue_transaction_take_success_ack(&transaction, 3u, 1u));
  x2_continue_transaction_begin(&transaction);
  x2_continue_transaction_reader_result(&transaction, 0);
  CHECK(!x2_continue_transaction_take_success_ack(&transaction, 3u, 1u));
  x2_continue_transaction_begin(&transaction);
  x2_continue_transaction_reader_result(&transaction, 1);
  CHECK(!x2_continue_transaction_take_success_ack(&transaction, 3u, 0u));
  CHECK(!x2_continue_transaction_take_success_ack(&transaction, 3u, 1u));
  x2_continue_transaction_begin(&transaction);
  x2_continue_transaction_reader_result(&transaction, 1);
  CHECK(x2_continue_transaction_take_success_ack(&transaction, 3u, 1u));
  CHECK(!x2_continue_transaction_take_success_ack(&transaction, 3u, 1u));

  printf("continue_policy: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
