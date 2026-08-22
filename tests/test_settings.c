#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

int main(void)
{
    const char *path = X2_TEST_SETTINGS_PATH;
    X2Settings saved, loaded, untouched;
    char why[256];
    FILE *file;

    remove(path);
    x2_settings_defaults(&saved);
    CHECK(saved.width == 1280 && saved.height == 720);
    CHECK(saved.window_mode == X2_WINDOW_WINDOWED);
    CHECK(saved.dynamic_shadows == 1 && saved.shadow_resolution == 1024);
    CHECK(saved.boot_mode == X2_BOOT_NORMAL);
    CHECK(strcmp(x2_boot_mode_label(saved.boot_mode), "Boot normally") == 0);
    CHECK(x2_boot_mode_parse("menu", &saved.boot_mode));
    CHECK(saved.boot_mode == X2_BOOT_MENU);
    CHECK(!x2_boot_mode_parse("new-game", &saved.boot_mode));
    CHECK(x2_settings_player_keyboard(&saved, 0) == 0);
    CHECK(x2_settings_player_keyboard(&saved, 1) == -1);

    saved.width = 1920;
    saved.height = 1080;
    saved.window_mode = X2_WINDOW_BORDERLESS;
    saved.dynamic_shadows = 0;
    saved.shadow_resolution = 2048;
    saved.boot_mode = X2_BOOT_CONTINUE;
    CHECK(x2_settings_assign_keyboard(&saved, 2, 0));
    CHECK(x2_settings_player_keyboard(&saved, 0) == 2);
    CHECK(saved.keyboard_player[0] == X2_SETTINGS_UNASSIGNED);
    CHECK(x2_settings_assign_controller(&saved, "sdl-045e-028e-a1", 0));
    CHECK(strcmp(x2_settings_player_controller(&saved, 0),
                 "sdl-045e-028e-a1") == 0);
    saved.keyboard_profile[2].keyboard_set[4] = 1;
    saved.keyboard_profile[2].keyboard[4] = 30;
    CHECK(x2_settings_save(&saved, path, why, sizeof why));
    CHECK(x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(memcmp(&saved, &loaded, sizeof saved) == 0);

    /* A device has one owner. P2-P4 have exactly one device kind when
       assigned, so changing kind evicts the other kind. */
    CHECK(x2_settings_assign_keyboard(&loaded, 1, 2));
    CHECK(x2_settings_assign_keyboard(&loaded, 3, 2));
    CHECK(loaded.keyboard_player[1] == X2_SETTINGS_UNASSIGNED);
    CHECK(x2_settings_assign_controller(&loaded, "pad-b", 2));
    CHECK(x2_settings_assign_controller(&loaded, "pad-c", 2));
    CHECK(x2_settings_player_keyboard(&loaded, 2) == -1);
    CHECK(x2_settings_controller_player(&loaded, "pad-b") ==
          X2_SETTINGS_UNASSIGNED);
    CHECK(x2_settings_controller_player(&loaded, "pad-c") == 2);

    /* Only P1 can hotswap. A controller assignment replaces P2's keyboard,
       while P1 retains its keyboard and controller together. */
    x2_settings_defaults(&loaded);
    CHECK(x2_settings_assign_keyboard(&loaded, 1, 1));
    CHECK(x2_settings_assign_controller(&loaded, "hot-pad-a", 0));
    CHECK(x2_settings_assign_controller(&loaded, "hot-pad-b", 1));
    CHECK(x2_settings_player_keyboard(&loaded, 0) == 0);
    CHECK(x2_settings_player_keyboard(&loaded, 1) == -1);
    CHECK(strcmp(x2_settings_player_controller(&loaded, 0), "hot-pad-a") == 0);
    CHECK(strcmp(x2_settings_player_controller(&loaded, 1), "hot-pad-b") == 0);

    /* P1 is the retail primary player and may not be left with no device. */
    CHECK(x2_settings_assign_controller(&loaded, "hot-pad-a",
                                        X2_SETTINGS_UNASSIGNED));
    CHECK(!x2_settings_assign_keyboard(&loaded, 0,
                                       X2_SETTINGS_UNASSIGNED));
    CHECK(x2_settings_player_keyboard(&loaded, 0) == 0);

    /* Old Auto migrates to its keyboard profile only; an old explicit pad is
       reserved by identity and never becomes roaming controller policy. */
    file = fopen(path, "w");
    assert(file);
    fprintf(file, "video.width=1280\nvideo.height=720\nvideo.mode=windowed\n"
                  "input.player0.device=auto\ninput.player0.profile=1\n"
                  "input.player1.device=gamepad:legacy-pad\n"
                  "input.player1.profile=0\n"
                  "input.player2.device=none\ninput.player2.profile=2\n"
                  "input.player3.device=none\ninput.player3.profile=3\n");
    fclose(file);
    CHECK(x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(x2_settings_player_keyboard(&loaded, 0) == 1);
    CHECK(strcmp(x2_settings_player_controller(&loaded, 1), "legacy-pad") == 0);
    CHECK(x2_settings_player_keyboard(&loaded, 1) == -1);

    /* Legacy allowed multiple players to reference one profile. The grid does
       not: later owners receive the lowest unreserved row with bindings cloned. */
    file = fopen(path, "w");
    assert(file);
    fprintf(file, "video.width=1280\nvideo.height=720\nvideo.mode=windowed\n"
                  "input.player0.device=keyboard\ninput.player0.profile=1\n"
                  "input.player1.device=auto\ninput.player1.profile=1\n"
                  "input.player2.device=none\ninput.player2.profile=2\n"
                  "input.player3.device=none\ninput.player3.profile=3\n"
                  "input.profile1.row4=77\n");
    fclose(file);
    CHECK(x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(x2_settings_player_keyboard(&loaded, 0) == 1);
    CHECK(x2_settings_player_keyboard(&loaded, 1) == 0);
    CHECK(loaded.keyboard_profile[0].keyboard_set[4] == 1);
    CHECK(loaded.keyboard_profile[0].keyboard[4] == 77);

    untouched = loaded;
    file = fopen(path, "w");
    assert(file);
    fprintf(file, "video.width=12\n");
    fclose(file);
    CHECK(!x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(memcmp(&loaded, &untouched, sizeof loaded) == 0);
    CHECK(strstr(why, ":1") != NULL);
    remove(path);

    printf("test_settings: %d checks passed\n", checks);
    return 0;
}
