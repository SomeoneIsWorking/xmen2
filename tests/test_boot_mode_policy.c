#include "boot_mode_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

int main(void)
{
    X2BootModeDecision decision;

    decision = x2_boot_mode_decide(X2_BOOT_NORMAL, 0);
    CHECK(decision.requested == X2_BOOT_NORMAL);
    CHECK(decision.effective == X2_BOOT_NORMAL);
    CHECK(!decision.fell_back_to_menu);

    decision = x2_boot_mode_decide(X2_BOOT_MENU, 1);
    CHECK(decision.requested == X2_BOOT_MENU);
    CHECK(decision.effective == X2_BOOT_MENU);
    CHECK(!decision.fell_back_to_menu);

    decision = x2_boot_mode_decide(X2_BOOT_CONTINUE, 1);
    CHECK(decision.requested == X2_BOOT_CONTINUE);
    CHECK(decision.effective == X2_BOOT_CONTINUE);
    CHECK(!decision.fell_back_to_menu);

    decision = x2_boot_mode_decide(X2_BOOT_CONTINUE, 0);
    CHECK(decision.requested == X2_BOOT_CONTINUE);
    CHECK(decision.effective == X2_BOOT_MENU);
    CHECK(decision.fell_back_to_menu);

    decision = x2_boot_mode_decide((X2BootMode)99, 1);
    CHECK(decision.requested == (X2BootMode)99);
    CHECK(decision.effective == X2_BOOT_NORMAL);
    CHECK(!decision.fell_back_to_menu);

    CHECK(x2_boot_mode_is_intro_command("runscript menus/intro_normal"));
    CHECK(!x2_boot_mode_is_intro_command("runscript menus/intro_normal_bad"));
    CHECK(!x2_boot_mode_is_intro_command("runscript menus/new_game"));
    CHECK(!x2_boot_mode_is_intro_command(NULL));

    printf("boot_mode_policy: %d checks passed\n", checks);
    return 0;
}
