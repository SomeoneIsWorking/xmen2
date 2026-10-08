#include "boot_mode_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

int main(void) {
  x2::native::BootModeDecision decision;

  decision = x2::native::boot_mode_decide(x2::config::BootMode::Normal, 0);
  CHECK(decision.requested == x2::config::BootMode::Normal);
  CHECK(decision.effective == x2::config::BootMode::Normal);
  CHECK(!decision.fell_back_to_menu);

  decision = x2::native::boot_mode_decide(x2::config::BootMode::Menu, 1);
  CHECK(decision.requested == x2::config::BootMode::Menu);
  CHECK(decision.effective == x2::config::BootMode::Menu);
  CHECK(!decision.fell_back_to_menu);

  decision = x2::native::boot_mode_decide(x2::config::BootMode::Continue, 1);
  CHECK(decision.requested == x2::config::BootMode::Continue);
  CHECK(decision.effective == x2::config::BootMode::Continue);
  CHECK(!decision.fell_back_to_menu);

  decision = x2::native::boot_mode_decide(x2::config::BootMode::Continue, 0);
  CHECK(decision.requested == x2::config::BootMode::Continue);
  CHECK(decision.effective == x2::config::BootMode::Menu);
  CHECK(decision.fell_back_to_menu);

  decision = x2::native::boot_mode_decide((x2::config::BootMode)99, 1);
  CHECK(decision.requested == (x2::config::BootMode)99);
  CHECK(decision.effective == x2::config::BootMode::Normal);
  CHECK(!decision.fell_back_to_menu);

  CHECK(x2::native::boot_mode_is_intro_command("runscript menus/intro_normal"));
  CHECK(!x2::native::boot_mode_is_intro_command(
      "runscript menus/intro_normal_bad"));
  CHECK(!x2::native::boot_mode_is_intro_command("runscript menus/new_game"));
  CHECK(!x2::native::boot_mode_is_intro_command(NULL));

  printf("boot_mode_policy: %d checks passed\n", checks);
  return 0;
}
