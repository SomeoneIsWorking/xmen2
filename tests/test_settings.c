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
    CHECK(saved.player[0].type == X2_PLAYER_AUTO);
    CHECK(saved.player[1].type == X2_PLAYER_NONE);

    saved.width = 1920;
    saved.height = 1080;
    saved.window_mode = X2_WINDOW_BORDERLESS;
    saved.player[0].type = X2_PLAYER_GAMEPAD;
    snprintf(saved.player[0].id, sizeof saved.player[0].id, "sdl-045e-028e-a1");
    saved.player[0].keyboard_profile = 2;
    saved.keyboard_profile[2].keyboard_set[4] = 1;
    saved.keyboard_profile[2].keyboard[4] = 30;
    CHECK(x2_settings_save(&saved, path, why, sizeof why));
    CHECK(x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(memcmp(&saved, &loaded, sizeof saved) == 0);

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
