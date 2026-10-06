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
  x2::save::ContinueMenuPlan plan;
  x2::save::ContinueTransaction transaction = {0};
  unsigned slot = 99u;
  unsigned i;

  x2::save::continue_menu_plan(0, 0, &plan);
  CHECK(plan.text[0] == x2::save::MainMenuText::NewGame);
  CHECK(plan.text[4] == x2::save::MainMenuText::Options);
  CHECK(plan.show_last_row); /* Play Online leads to LAN play */
  CHECK(plan.danger_row == 2u);
  CHECK(!plan.disable_online_special);
  for (i = 0; i < x2::save::kMainMenuRows; i++)
    CHECK(plan.command_source[i] == i);

  x2::save::continue_menu_plan(1, 0, &plan);
  CHECK(plan.text[0] == x2::save::MainMenuText::Continue);
  CHECK(plan.text[1] == x2::save::MainMenuText::NewGame);
  CHECK(plan.text[2] == x2::save::MainMenuText::LoadGame);
  CHECK(plan.text[3] == x2::save::MainMenuText::DangerRoom);
  CHECK(plan.text[4] == x2::save::MainMenuText::Review);
  CHECK(plan.text[5] == x2::save::MainMenuText::Options);
  CHECK(plan.show_last_row);
  CHECK(plan.danger_row == 3u);
  CHECK(plan.disable_online_special);
  for (i = 0; i < x2::save::kMainMenuRows; i++)
    CHECK(plan.command_source[i] == (i ? i - 1u : 6u));

  /* A LAN game leads; Play Online, then Review, make room for it. */
  x2::save::continue_menu_plan(0, 1, &plan);
  CHECK(plan.text[0] == x2::save::MainMenuText::JoinLan);
  CHECK(plan.command_source[0] == x2::save::kMenuCommandJoinLan);
  CHECK(plan.text[1] == x2::save::MainMenuText::NewGame);
  CHECK(plan.text[4] == x2::save::MainMenuText::Review);
  CHECK(plan.text[5] == x2::save::MainMenuText::Options);
  CHECK(plan.command_source[5] == 4u);
  CHECK(plan.danger_row == 3u);
  CHECK(plan.disable_online_special);

  x2::save::continue_menu_plan(1, 1, &plan);
  CHECK(plan.text[0] == x2::save::MainMenuText::JoinLan);
  CHECK(plan.text[1] == x2::save::MainMenuText::Continue);
  CHECK(plan.command_source[1] == x2::save::kMenuCommandContinue);
  CHECK(plan.text[2] == x2::save::MainMenuText::NewGame);
  CHECK(plan.text[4] == x2::save::MainMenuText::DangerRoom);
  CHECK(plan.text[5] == x2::save::MainMenuText::Options);
  CHECK(plan.danger_row == 4u);
  CHECK(plan.disable_online_special);

  CHECK(x2::save::continue_leaf_slot("autosave.save", &slot) && slot == 0u);
  CHECK(x2::save::continue_leaf_slot("saveslot0.save", &slot) && slot == 0u);
  CHECK(x2::save::continue_leaf_slot("saveslot9.save", &slot) && slot == 9u);
  CHECK(!x2::save::continue_leaf_slot("saveslot10.save", &slot));
  CHECK(!x2::save::continue_leaf_slot("../saveslot0.save", &slot));
  CHECK(!x2::save::continue_leaf_slot(NULL, &slot));
  CHECK(!x2::save::continue_leaf_slot("autosave.save", NULL));

  /* Manual Load never arms native Continue's one-shot. */
  CHECK(!x2::save::continue_transaction_take_success_ack(&transaction, 3u, 1u));
  x2::save::continue_transaction_begin(&transaction);
  x2::save::continue_transaction_reader_result(&transaction, 0);
  CHECK(!x2::save::continue_transaction_take_success_ack(&transaction, 3u, 1u));
  x2::save::continue_transaction_begin(&transaction);
  x2::save::continue_transaction_reader_result(&transaction, 1);
  CHECK(!x2::save::continue_transaction_take_success_ack(&transaction, 3u, 0u));
  CHECK(!x2::save::continue_transaction_take_success_ack(&transaction, 3u, 1u));
  x2::save::continue_transaction_begin(&transaction);
  x2::save::continue_transaction_reader_result(&transaction, 1);
  CHECK(x2::save::continue_transaction_take_success_ack(&transaction, 3u, 1u));
  CHECK(!x2::save::continue_transaction_take_success_ack(&transaction, 3u, 1u));

  printf("continue_policy: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
